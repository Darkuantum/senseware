<template>
  <div class="connection-panel">
    <div class="panel-row">
      <button
        class="btn btn-primary"
        :disabled="state === 'connecting' || !isSupported"
        @click="handleToggle"
      >
        <span v-if="state === 'connecting'" class="spinner" />
        <span v-else-if="state === 'connected'" class="icon disconnect">&#x2715;</span>
        <span v-else class="icon connect">&#x2795;</span>
        {{ buttonText }}
      </button>

      <span class="status-badge" :class="statusClass">
        <span class="status-dot" />
        {{ statusLabel }}
      </span>

      <span v-if="deviceName" class="device-name">{{ deviceName }}</span>
    </div>

    <p v-if="!isSupported" class="warning">
      Web Bluetooth is not supported in this browser. Please use
      <strong>Google Chrome</strong> or <strong>Microsoft Edge</strong> on
      <strong>HTTPS</strong> or <strong>localhost</strong>.
    </p>

    <p v-if="isSupported && !isLocalhost" class="hint">
      Web Bluetooth requires HTTPS. Make sure this page is served over a secure
      connection or via localhost.
    </p>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { useBluetooth, STATE } from '../composables/useBluetooth.js'

const ble = useBluetooth()

const state = ref(STATE.DISCONNECTED)
const deviceName = ref('')
const isLocalhost = ref(false)
const isSupported = ble.isSupported
const isAlerting = ref(false)

const buttonText = computed(() => {
  switch (state.value) {
    case STATE.CONNECTING: return 'Connecting...'
    case STATE.CONNECTED: return 'Disconnect'
    case STATE.DISCONNECTING: return 'Disconnecting...'
    default: return 'Connect to Device'
  }
})

const statusClass = computed(() => {
  if (isAlerting.value) return 'alerting'
  switch (state.value) {
    case STATE.CONNECTED: return 'connected'
    case STATE.CONNECTING: return 'connecting'
    case STATE.DISCONNECTING: return 'disconnecting'
    default: return 'disconnected'
  }
})

const statusLabel = computed(() => {
  if (isAlerting.value) return 'Alerting'
  switch (state.value) {
    case STATE.CONNECTED: return 'Connected'
    case STATE.CONNECTING: return 'Connecting'
    case STATE.DISCONNECTING: return 'Disconnecting'
    default: return 'Disconnected'
  }
})

onMounted(() => {
  isLocalhost.value = window.location.hostname === 'localhost' ||
    window.location.hostname === '127.0.0.1'
})

const unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ble.on('stateChange', ({ new: newState }) => {
      state.value = newState
    })
  )

  unsubscribers.push(
    ble.on('connected', (data) => {
      deviceName.value = data.name || 'Senseware'
    })
  )

  unsubscribers.push(
    ble.on('reconnected', (data) => {
      deviceName.value = data.name || 'Senseware'
    })
  )

  // Alert state — go into alerting mode when anomaly detected, return after 5s
  let alertTimeout = null
  unsubscribers.push(
    ble.on('alert', ({ anomaly }) => {
      if (anomaly) {
        isAlerting.value = true
        if (alertTimeout) clearTimeout(alertTimeout)
        alertTimeout = setTimeout(() => {
          isAlerting.value = false
          alertTimeout = null
        }, 5000)
      }
    })
  )
})

onUnmounted(() => {
  unsubscribers.forEach((unsub) => unsub())
})

async function handleToggle() {
  if (state.value === STATE.CONNECTED || state.value === STATE.CONNECTING) {
    await ble.disconnect()
  } else {
    try {
      await ble.connect()
    } catch {
      // User cancelled or error — handled in composable
    }
  }
}
</script>

<script>
export default {
  compatConfig: { MODE: 3 },
}
</script>

<style scoped>
.connection-panel {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 1rem 1.25rem;
}

.panel-row {
  display: flex;
  align-items: center;
  gap: 1rem;
  flex-wrap: wrap;
}

.btn {
  display: inline-flex;
  align-items: center;
  gap: 0.5rem;
  padding: 0.6rem 1.25rem;
  border: none;
  border-radius: 8px;
  font-size: 0.9rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.2s ease;
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-primary {
  background: var(--accent);
  color: #fff;
}

.btn-primary:hover:not(:disabled) {
  background: var(--accent-hover);
  transform: translateY(-1px);
}

.icon {
  font-size: 1rem;
}

.status-badge {
  display: inline-flex;
  align-items: center;
  gap: 0.4rem;
  padding: 0.3rem 0.75rem;
  border-radius: 20px;
  font-size: 0.8rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
}

.status-badge.disconnected {
  background: rgba(107, 114, 128, 0.2);
  color: #9ca3af;
}
.status-badge.disconnected .status-dot {
  background: #6b7280;
}

.status-badge.connected {
  background: rgba(34, 197, 94, 0.15);
  color: #22c55e;
}
.status-badge.connected .status-dot {
  background: #22c55e;
  box-shadow: 0 0 6px #22c55e;
  animation: pulse-green 2s infinite;
}

.status-badge.connecting,
.status-badge.disconnecting {
  background: rgba(234, 179, 8, 0.15);
  color: #eab308;
}
.status-badge.connecting .status-dot {
  background: #eab308;
  animation: pulse-yellow 1s infinite;
}
.status-badge.disconnecting .status-dot {
  background: #eab308;
}

.status-badge.alerting {
  background: rgba(239, 68, 68, 0.15);
  color: #ef4444;
}
.status-badge.alerting .status-dot {
  background: #ef4444;
  box-shadow: 0 0 8px #ef4444;
  animation: pulse-red 0.8s infinite;
}

.device-name {
  color: var(--text-secondary);
  font-size: 0.85rem;
  font-family: var(--mono);
}

.warning {
  margin-top: 0.75rem;
  padding: 0.6rem 0.8rem;
  background: rgba(234, 179, 8, 0.1);
  border-left: 3px solid #eab308;
  border-radius: 4px;
  color: #fbbf24;
  font-size: 0.82rem;
  line-height: 1.4;
}

.hint {
  margin-top: 0.5rem;
  color: var(--text-muted);
  font-size: 0.78rem;
}

.spinner {
  display: inline-block;
  width: 14px;
  height: 14px;
  border: 2px solid rgba(255, 255, 255, 0.3);
  border-top-color: #fff;
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

@keyframes pulse-green {
  0%, 100% { box-shadow: 0 0 4px #22c55e; }
  50% { box-shadow: 0 0 10px #22c55e; }
}

@keyframes pulse-yellow {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.4; }
}

@keyframes pulse-red {
  0%, 100% { box-shadow: 0 0 6px #ef4444; }
  50% { box-shadow: 0 0 14px #ef4444; }
}
</style>
