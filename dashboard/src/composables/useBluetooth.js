// useBluetooth.js — Web Bluetooth API composable for Senseware BLE GATT

const DEVICE_NAME = 'Senseware'
const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b'
const TELEMETRY_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8'
const ALERT_UUID = '1c95d6e4-5dc9-4659-9290-43283a3b8d5a'

const MAX_RECONNECT_ATTEMPTS = 10
const BASE_DELAY = 3000

export const STATE = {
  DISCONNECTED: 'disconnected',
  CONNECTING: 'connecting',
  CONNECTED: 'connected',
  DISCONNECTING: 'disconnecting',
}

// Simple reactive event emitter
class BluetoothManager {
  constructor() {
    this.state = STATE.DISCONNECTED
    this.device = null
    this.server = null
    this.telemetryChar = null
    this.alertChar = null
    this.listeners = new Map()
    this.reconnectTimer = null
    this.lastValues = { heartRate: 0, spo2: 0, emgEnvelope: 0, motionMagnitude: 0 }
    this._intentionalDisconnect = false
    this._onDisconnected = null
    this._reconnectAttempts = 0
    // Stable bound references — prevents duplicate listeners on reconnect
    this._boundHandleTelemetry = this._handleTelemetry.bind(this)
    this._boundHandleAlert = this._handleAlert.bind(this)
  }

  get isConnected() {
    return this.state === STATE.CONNECTED
  }

  get isSupported() {
    return typeof navigator !== 'undefined' && navigator.bluetooth !== undefined
  }

  on(event, callback) {
    if (!this.listeners.has(event)) {
      this.listeners.set(event, new Set())
    }
    this.listeners.get(event).add(callback)
    // Return unsubscribe function
    return () => {
      this.listeners.get(event)?.delete(callback)
    }
  }

  emit(event, data) {
    this.listeners.get(event)?.forEach((cb) => {
      try {
        cb(data)
      } catch (err) {
        console.error(`[BLE] Event handler error for "${event}":`, err)
      }
    })
  }

  setState(newState) {
    if (this.state !== newState) {
      const old = this.state
      this.state = newState
      this.emit('stateChange', { old, new: newState })
    }
  }

  async connect() {
    if (!this.isSupported) {
      throw new Error('Web Bluetooth is not supported in this browser. Use Chrome or Edge on HTTPS/localhost.')
    }

    if (this.isConnected) {
      console.warn('[BLE] Already connected')
      return
    }

    try {
      this.setState(STATE.CONNECTING)

      // Request device — Chrome shows a pairing dialog
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ name: DEVICE_NAME }],
        optionalServices: [SERVICE_UUID],
      })

      this.emit('deviceFound', { name: this.device.name })

      // Remove previous listener to prevent accumulation (#2)
      if (this._onDisconnected && this.device) {
        this.device.removeEventListener('gattserverdisconnected', this._onDisconnected)
      }

      // Listen for disconnection from device side (#1: respect intentional flag)
      this._onDisconnected = () => {
        console.warn('[BLE] Device disconnected unexpectedly')
        this.telemetryChar = null
        this.alertChar = null
        this.server = null
        this.setState(STATE.DISCONNECTED)
        this.emit('telemetry', { heartRate: 0, spo2: 0, emgEnvelope: 0, motionMagnitude: 0 })
        if (!this._intentionalDisconnect) {
          this._reconnectAttempts = 0
          this.attemptReconnect()
        }
        this._intentionalDisconnect = false
      }
      this.device.addEventListener('gattserverdisconnected', this._onDisconnected)

      // Connect GATT
      this.server = await this.device.gatt.connect()

      // Get service
      const service = await this.server.getPrimaryService(SERVICE_UUID)

      // Get characteristics
      this.telemetryChar = await service.getCharacteristic(TELEMETRY_UUID)
      this.alertChar = await service.getCharacteristic(ALERT_UUID)

      // Subscribe to telemetry notifications
      // Fix #2A: register listener BEFORE startNotifications() to avoid
      // losing the first notification if the ESP32 sends it immediately
      // after the CCCD write is acknowledged.
      this.telemetryChar.addEventListener(
        'characteristicvaluechanged',
        this._boundHandleTelemetry
      )
      await this.telemetryChar.startNotifications()

      // Subscribe to alert notifications (same ordering fix)
      this.alertChar.addEventListener(
        'characteristicvaluechanged',
        this._boundHandleAlert
      )
      await this.alertChar.startNotifications()

      this._reconnectAttempts = 0
      this.setState(STATE.CONNECTED)
      this.emit('connected', { name: this.device.name })
      console.info('[BLE] Connected to', this.device.name)
    } catch (err) {
      if (err.name === 'NotFoundError') {
        // User cancelled the device picker
        console.info('[BLE] User cancelled device selection')
      } else {
        console.error('[BLE] Connection error:', err)
        this.emit('error', { message: err.message })
      }
      this.setState(STATE.DISCONNECTED)
      throw err
    }
  }

  async disconnect() {
    this._intentionalDisconnect = true
    this._clearReconnectTimer()
    this._reconnectAttempts = 0

    if (!this.device?.gatt?.connected) {
      this.setState(STATE.DISCONNECTED)
      return
    }

    try {
      this.setState(STATE.DISCONNECTING)
      // Fix #2C/#2E: remove listeners and stop notifications before disconnecting
      // to prevent stale subscription state in the browser's BLE stack
      if (this.telemetryChar) {
        this.telemetryChar.removeEventListener('characteristicvaluechanged', this._boundHandleTelemetry)
        try { await this.telemetryChar.stopNotifications() } catch { /* may already be stopped */ }
      }
      if (this.alertChar) {
        this.alertChar.removeEventListener('characteristicvaluechanged', this._boundHandleAlert)
        try { await this.alertChar.stopNotifications() } catch { /* may already be stopped */ }
      }
      this.telemetryChar = null
      this.alertChar = null
      this.server = null
      this.device.gatt.disconnect()
      // Do NOT set DISCONNECTED here — the gattserverdisconnected handler does it
    } catch (err) {
      console.error('[BLE] Disconnect error:', err)
      this.setState(STATE.DISCONNECTED)
    }
  }

  attemptReconnect() {
    if (this.reconnectTimer) return

    // Retry cap (#10)
    this._reconnectAttempts++
    if (this._reconnectAttempts > MAX_RECONNECT_ATTEMPTS) {
      this.setState(STATE.DISCONNECTED)
      this.emit('error', { message: `Reconnect failed after ${MAX_RECONNECT_ATTEMPTS} attempts` })
      this._clearReconnectTimer()
      return
    }

    const delay = BASE_DELAY * Math.min(this._reconnectAttempts, 5)
    console.info(`[BLE] Will attempt reconnection in ${delay / 1000}s (attempt ${this._reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS})...`)
    this.reconnectTimer = setTimeout(async () => {
      this.reconnectTimer = null

      if (!this.device) {
        this.setState(STATE.DISCONNECTED)
        return
      }

      try {
        console.info('[BLE] Attempting reconnect to', this.device.name)
        this.setState(STATE.CONNECTING)
        this.server = await this.device.gatt.connect()

        const service = await this.server.getPrimaryService(SERVICE_UUID)
        this.telemetryChar = await service.getCharacteristic(TELEMETRY_UUID)
        this.alertChar = await service.getCharacteristic(ALERT_UUID)

        // Fix #2A/#2B: add listener BEFORE startNotifications;
        // use stable bound refs so duplicates are avoided
        this.telemetryChar.addEventListener(
          'characteristicvaluechanged',
          this._boundHandleTelemetry
        )
        await this.telemetryChar.startNotifications()

        this.alertChar.addEventListener(
          'characteristicvaluechanged',
          this._boundHandleAlert
        )
        await this.alertChar.startNotifications()

        this._reconnectAttempts = 0
        this.setState(STATE.CONNECTED)
        this.emit('reconnected', { name: this.device.name })
        console.info('[BLE] Reconnected successfully')
      } catch (err) {
        console.warn('[BLE] Reconnect failed, retrying:', err.message)
        this.setState(STATE.DISCONNECTED)
        this.attemptReconnect()
      }
    }, delay)
  }

  _clearReconnectTimer() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
  }

  /**
   * Parse telemetry notification: 16 bytes = 4 little-endian Float32
   *   offset 0:  heart_rate (BPM)
   *   offset 4:  spo2 (%)
   *   offset 8:  emg_envelope (μV)
   *   offset 12: motion_magnitude (g)
   */
  _handleTelemetry(event) {
    // Fix #2D: wrap in try/catch — an unhandled error in this handler
    // can cause Chrome to stop dispatching further characteristicvaluechanged
    // events, silently freezing the data stream.
    try {
      // event.target.value is already a DataView — no wrapping needed
      const view = event.target.value

      if (view.byteLength >= 16) {
        const heartRate = view.getFloat32(0, true)        // little-endian
        const spo2 = view.getFloat32(4, true)             // little-endian
        const emgEnvelope = view.getFloat32(8, true)       // little-endian
        const motionMagnitude = view.getFloat32(12, true)   // little-endian

        this.lastValues = { heartRate, spo2, emgEnvelope, motionMagnitude }

        this.emit('telemetry', {
          heartRate,
          spo2,
          emgEnvelope,
          motionMagnitude,
          timestamp: Date.now(),
        })
      }
    } catch (err) {
      console.error('[BLE] Telemetry parse error:', err)
    }
  }

  /**
   * Parse alert notification: 1 byte
   *   0x00 = normal
   *   0x01 = anomaly (allostatic spike)
   */
  _handleAlert(event) {
    try {
      const value = event.target.value.getUint8(0)
      const isAnomaly = value === 0x01

      this.emit('alert', {
        anomaly: isAnomaly,
        timestamp: Date.now(),
        heartRate: this.lastValues.heartRate,
        spo2: this.lastValues.spo2,
        emgEnvelope: this.lastValues.emgEnvelope,
        motionMagnitude: this.lastValues.motionMagnitude,
      })
    } catch (err) {
      console.error('[BLE] Alert parse error:', err)
    }
  }
}

// Singleton instance
const bleManager = new BluetoothManager()

/**
 * Vue 3 composable for Web Bluetooth
 */
export function useBluetooth() {
  const connect = () => bleManager.connect()
  const disconnect = () => bleManager.disconnect()
  const on = (event, callback) => bleManager.on(event, callback)

  return {
    connect,
    disconnect,
    on,
    get state() {
      return bleManager.state
    },
    get isConnected() {
      return bleManager.isConnected
    },
    get isSupported() {
      return bleManager.isSupported
    },
    get lastValues() {
      return { ...bleManager.lastValues }
    },
    STATE,
  }
}
