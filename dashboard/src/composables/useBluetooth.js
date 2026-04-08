// useBluetooth.js — Web Bluetooth API composable for Senseware BLE GATT

const DEVICE_NAME = 'Senseware'
const SERVICE_UUID = '4fafc201-1fb5-459e-8fcc-c5c9c331914b'
const TELEMETRY_UUID = 'beb5483e-36e1-4688-b7f5-ea07361b26a8'
const ALERT_UUID = '1c95d6e4-5dc9-4659-9290-43283a3b8d5a'

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
    this.lastValues = { heartRate: 0, emgEnvelope: 0, motionMagnitude: 0 }
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

      // Listen for disconnection from device side
      this.device.addEventListener('gattserverdisconnected', () => {
        console.warn('[BLE] Device disconnected unexpectedly')
        this.telemetryChar = null
        this.alertChar = null
        this.server = null
        this.setState(STATE.DISCONNECTED)
        this.emit('telemetry', { heartRate: 0, emgEnvelope: 0, motionMagnitude: 0 })
        this.attemptReconnect()
      })

      // Connect GATT
      this.server = await this.device.gatt.connect()

      // Get service
      const service = await this.server.getPrimaryService(SERVICE_UUID)

      // Get characteristics
      this.telemetryChar = await service.getCharacteristic(TELEMETRY_UUID)
      this.alertChar = await service.getCharacteristic(ALERT_UUID)

      // Subscribe to telemetry notifications
      await this.telemetryChar.startNotifications()
      this.telemetryChar.addEventListener(
        'characteristicvaluechanged',
        this._handleTelemetry.bind(this)
      )

      // Subscribe to alert notifications
      await this.alertChar.startNotifications()
      this.alertChar.addEventListener(
        'characteristicvaluechanged',
        this._handleAlert.bind(this)
      )

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
    this._clearReconnectTimer()

    if (!this.device?.gatt?.connected) {
      this.setState(STATE.DISCONNECTED)
      return
    }

    try {
      this.setState(STATE.DISCONNECTING)
      this.device.gatt.disconnect()
    } catch (err) {
      console.error('[BLE] Disconnect error:', err)
    } finally {
      this.telemetryChar = null
      this.alertChar = null
      this.server = null
      this.setState(STATE.DISCONNECTED)
      this.emit('disconnected')
    }
  }

  attemptReconnect() {
    if (this.reconnectTimer) return

    console.info('[BLE] Will attempt reconnection in 3s...')
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

        await this.telemetryChar.startNotifications()
        this.telemetryChar.addEventListener(
          'characteristicvaluechanged',
          this._handleTelemetry.bind(this)
        )

        await this.alertChar.startNotifications()
        this.alertChar.addEventListener(
          'characteristicvaluechanged',
          this._handleAlert.bind(this)
        )

        this.setState(STATE.CONNECTED)
        this.emit('reconnected', { name: this.device.name })
        console.info('[BLE] Reconnected successfully')
      } catch (err) {
        console.warn('[BLE] Reconnect failed, retrying:', err.message)
        this.setState(STATE.DISCONNECTED)
        this.attemptReconnect()
      }
    }, 3000)
  }

  _clearReconnectTimer() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
  }

  /**
   * Parse telemetry notification: 12 bytes = 3 little-endian Float32
   *   offset 0:  heart_rate (BPM)
   *   offset 4:  emg_envelope (μV)
   *   offset 8:  motion_magnitude (g)
   */
  _handleTelemetry(event) {
    const view = new DataView(event.target.value.buffer)

    const heartRate = view.getFloat32(0, true)        // little-endian
    const emgEnvelope = view.getFloat32(4, true)       // little-endian
    const motionMagnitude = view.getFloat32(8, true)   // little-endian

    this.lastValues = { heartRate, emgEnvelope, motionMagnitude }

    this.emit('telemetry', {
      heartRate,
      emgEnvelope,
      motionMagnitude,
      timestamp: Date.now(),
    })
  }

  /**
   * Parse alert notification: 1 byte
   *   0x00 = normal
   *   0x01 = anomaly (allostatic spike)
   */
  _handleAlert(event) {
    const value = event.target.value.getUint8(0)
    const isAnomaly = value === 0x01

    this.emit('alert', {
      anomaly: isAnomaly,
      timestamp: Date.now(),
      heartRate: this.lastValues.heartRate,
      emgEnvelope: this.lastValues.emgEnvelope,
      motionMagnitude: this.lastValues.motionMagnitude,
    })
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
