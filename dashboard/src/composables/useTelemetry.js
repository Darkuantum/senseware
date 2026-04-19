// useTelemetry.js — Server-Sent Events composable for Senseware ESP32
// Connects to http://{ip}:81/events and receives real-time telemetry push

import { ref, onUnmounted } from 'vue'

class EventSourceManager {
  constructor() {
    this.source = null
    this._listeners = new Map()
    this._lastIp = ''
    this._messageCount = 0
    this._lastAnomaly = 0
    this._reconnectTimer = null
    this._reconnectDebounceMs = 2000
    this._wasConnected = false
  }

  on(event, callback) {
    if (!this._listeners.has(event)) {
      this._listeners.set(event, new Set())
    }
    this._listeners.get(event).add(callback)
    return () => this._listeners.get(event)?.delete(callback)
  }

  off(event, callback) {
    this._listeners.get(event)?.delete(callback)
  }

  _emit(event, data) {
    const listeners = this._listeners.get(event)
    if (listeners) {
      listeners.forEach(cb => {
        try { cb(data) } catch (e) { console.error(`[Telemetry] Event "${event}" error:`, e) }
      })
    }
  }

  connect(ip) {
    this.disconnect()
    this._lastIp = ip
    this._messageCount = 0
    this._lastAnomaly = 0
    this._emit('stateChange', 'connecting')

    const url = `http://${ip}:81/events`
    console.log(`[Telemetry] Connecting to ${url}`)

    this.source = new EventSource(url)

    this.source.onopen = () => {
      if (this._reconnectTimer) {
        clearTimeout(this._reconnectTimer)
        this._reconnectTimer = null
      }
      if (!this._wasConnected) {
        this._wasConnected = true
        console.log('[Telemetry] Connected')
        this._emit('stateChange', 'connected')
      } else {
        // Silent reconnect — already showed connected state, don't re-emit
        console.log('[Telemetry] Reconnected silently')
      }
    }

    this.source.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data)
        this._messageCount++

        this._emit('telemetry', data)

        // Rising-edge detection: alert only on 0→1 transition
        const isAnomaly = data.anomaly === 1
        const risingEdge = isAnomaly && !this._lastAnomaly
        this._lastAnomaly = isAnomaly ? 1 : 0
        this._emit('alert', { alert: risingEdge, ...data })
      } catch (e) {
        console.error('[Telemetry] Parse error:', e, event.data)
      }
    }

    this.source.onerror = () => {
      const readyState = this.source ? this.source.readyState : EventSource.CLOSED
      if (readyState === EventSource.CLOSED) {
        // Connection permanently closed — clear any pending reconnect timer and emit immediately
        if (this._reconnectTimer) {
          clearTimeout(this._reconnectTimer)
          this._reconnectTimer = null
        }
        this._wasConnected = false
        console.warn('[Telemetry] Connection closed — will not auto-reconnect')
        this._emit('stateChange', 'disconnected')
        this._emit('error', { message: 'Connection closed by server' })
        this.source = null
      } else if (readyState === EventSource.CONNECTING) {
        // Auto-reconnecting — debounce to avoid flicker
        if (!this._reconnectTimer) {
          this._reconnectTimer = setTimeout(() => {
            this._reconnectTimer = null
            // Still reconnecting after debounce — emit disconnected
            if (this.source && this.source.readyState === EventSource.CONNECTING) {
              this._wasConnected = false
              this._emit('stateChange', 'disconnected')
              this._emit('error', { message: 'Reconnection taking too long' })
            }
          }, this._reconnectDebounceMs)
        }
      }
    }
  }

  disconnect() {
    if (this._reconnectTimer) {
      clearTimeout(this._reconnectTimer)
      this._reconnectTimer = null
    }
    if (this.source) {
      this.source.close()
      this.source = null
    }
    this._wasConnected = false
    this._emit('stateChange', 'disconnected')
  }

  get isConnected() {
    return this.source !== null && this.source.readyState === EventSource.OPEN
  }
}

// Singleton
const manager = new EventSourceManager()

/**
 * Vue 3 composable for Server-Sent Events connection to ESP32
 *
 * ESP32 SSE endpoint: http://{ip}:81/events
 * Message format: {"hr":72.0,"spo2":98.0,"emg":50.0,"mot":1.03,"mse":0.0005,"acc":95.0,"anomaly":0}
 *
 * Events:
 *   'telemetry'   — {hr, spo2, emg, mot, mse, acc, anomaly}
 *   'alert'       — {alert: true/false, ...telemetryData}
 *   'stateChange' — 'disconnected' | 'connecting' | 'connected'
 *   'error'       — {message: string}
 */
export function useTelemetry() {
  const state = ref('disconnected')
  const ip = ref('')
  const latestTelemetry = ref(null)
  const alerts = ref([])

  const onTelemetry = (data) => {
    latestTelemetry.value = data
  }

  const onAlert = (data) => {
    if (data.alert) {
      alerts.value.unshift({
        timestamp: new Date(),
        hr: data.hr,
        emg: data.emg,
        mot: data.mot,
        mse: data.mse,
        acc: data.acc,
      })
      if (alerts.value.length > 200) alerts.value.pop()
    }
  }

  const onStateChange = (newState) => {
    state.value = newState
    if (newState === 'disconnected') {
      latestTelemetry.value = null
    }
  }

  const unsubTelemetry = manager.on('telemetry', onTelemetry)
  const unsubAlert = manager.on('alert', onAlert)
  const unsubState = manager.on('stateChange', onStateChange)

  onUnmounted(() => {
    unsubTelemetry()
    unsubAlert()
    unsubState()
  })

  return {
    state,
    ip,
    latestTelemetry,
    alerts,
    connect: (newIp) => { ip.value = newIp; manager.connect(newIp) },
    disconnect: () => manager.disconnect(),
    isConnected: manager.isConnected,
    manager,
  }
}
