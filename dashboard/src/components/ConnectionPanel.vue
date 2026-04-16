<template>
  <div class="connection-panel">
    <div class="panel-row">
      <button
        class="btn btn-primary"
        :disabled="state === 'connecting' || state === 'disconnecting' || !isSupported"
        @click="handleToggle"
      >
        <span v-if="state === 'connecting'" class="spinner" />
        <BluetoothSearching v-else-if="state === 'connected'" :size="16" class="btn-icon" />
        <Bluetooth v-else :size="16" class="btn-icon" />
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

    <p v-if="isSupported && !isSecureContext" class="hint">
      Web Bluetooth requires HTTPS. Make sure this page is served over a secure
      connection or via localhost.
    </p>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { Bluetooth, BluetoothSearching } from 'lucide-vue-next'
import { useBluetooth, STATE } from '../composables/useBluetooth.js'

const ble = useBluetooth()

const state = ref(STATE.DISCONNECTED)
const deviceName = ref('')
const isSecureContext = ref(false)
const isSupported = ble.isSupported
const isAlerting = ref(false)

const buttonText = computed(() => {
  switch (state.value) {
    case STATE.CONNECTING: return 'Connecting...'
    case STATE.CONNECTED: return 'Disconnect'
    case STATE.DISCONNECTING: return 'Disconnecting...'
    default: return 'Connect Device'
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
  isSecureContext.value = window.isSecureContext
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
  if (state.value === STATE.CONNECTED || state.value === STATE.CONNECTING || state.value === STATE.DISCONNECTING) {
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

<style scoped>
.connection-panel {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 0.75rem 1rem;
}

.panel-row {
  display: flex;
  align-items: center;
  gap: 0.85rem;
  flex-wrap: wrap;
}

.btn {
  display: inline-flex;
  align-items: center;
  gap: 0.45rem;
  padding: 0.5rem 1.1rem;
  border: none;
  border-radius: var(--radius-sm);
  font-size: 0.85rem;
  font-weight: 600;
  cursor: pointer;
  transition: all var(--transition-normal);
}

.btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.btn-primary {
  background: var(--accent);
  color: var(--bg-primary);
}

.btn-primary:hover:not(:disabled) {
  background: var(--accent-hover);
  transform: translateY(-1px);
}

.btn-icon {
  flex-shrink: 0;
}

.status-badge {
  display: inline-flex;
  align-items: center;
  gap: 0.4rem;
  padding: 0.28rem 0.7rem;
  border-radius: 100px;
  font-size: 0.75rem;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.status-badge.disconnected {
  background: rgba(138, 123, 107, 0.15);
  color: var(--text-muted);
}
.status-badge.disconnected .status-dot {
  background: var(--text-muted);
}

.status-badge.connected {
  background: rgba(90, 184, 143, 0.15);
  color: var(--color-success);
}
.status-badge.connected .status-dot {
  background: var(--color-success);
  box-shadow: 0 0 6px var(--color-success);
  animation: pulse-green 2s infinite;
}

.status-badge.connecting,
.status-badge.disconnecting {
  background: rgba(232, 177, 58, 0.15);
  color: var(--color-warning);
}
.status-badge.connecting .status-dot {
  background: var(--color-warning);
  animation: pulse-yellow 1s infinite;
}
.status-badge.disconnecting .status-dot {
  background: var(--color-warning);
}

.status-badge.alerting {
  background: rgba(232, 93, 74, 0.15);
  color: var(--color-danger);
}
.status-badge.alerting .status-dot {
  background: var(--color-danger);
  box-shadow: 0 0 8px var(--color-danger);
  animation: pulse-red 0.8s infinite;
}

.device-name {
  color: var(--text-secondary);
  font-size: 0.82rem;
  font-family: var(--mono);
}

.warning {
  margin-top: 0.65rem;
  padding: 0.55rem 0.75rem;
  background: rgba(232, 177, 58, 0.08);
  border-left: 3px solid var(--color-warning);
  border-radius: 4px;
  color: #f0c85a;
  font-size: 0.8rem;
  line-height: 1.4;
}

.hint {
  margin-top: 0.5rem;
  color: var(--text-muted);
  font-size: 0.75rem;
}

.spinner {
  display: inline-block;
  width: 14px;
  height: 14px;
  border: 2px solid rgba(0, 0, 0, 0.2);
  border-top-color: var(--bg-primary);
  border-radius: 50%;
  animation: spin 0.7s linear infinite;
}

@keyframes spin {
  to { transform: rotate(360deg); }
}

@keyframes pulse-green {
  0%, 100% { box-shadow: 0 0 4px var(--color-success); }
  50% { box-shadow: 0 0 10px var(--color-success); }
}

@keyframes pulse-yellow {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.4; }
}

@keyframes pulse-red {
  0%, 100% { box-shadow: 0 0 6px var(--color-danger); }
  50% { box-shadow: 0 0 14px var(--color-danger); }
}
</style>
