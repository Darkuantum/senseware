<template>
  <div class="connection-panel">
    <div class="panel-row">
      <!-- IP Input + Connect Button (when disconnected) -->
      <template v-if="state === 'disconnected'">
        <div class="ip-input-group">
          <Wifi :size="14" class="ip-icon" />
          <input
            v-model="ipInput"
            type="text"
            class="ip-input"
            placeholder="192.168.x.x"
            :disabled="state === 'connecting'"
            @keyup.enter="handleConnect"
          />
          <span class="ip-port">:81</span>
        </div>
        <button
          class="btn btn-primary"
          :disabled="!ipInput.trim() || state === 'connecting'"
          @click="handleConnect"
        >
          Connect
        </button>
      </template>

      <!-- Connecting state -->
      <template v-else-if="state === 'connecting'">
        <button class="btn btn-primary" disabled>
          <span class="spinner" />
          Connecting to {{ ip }}...
        </button>
        <button class="btn btn-ghost" @click="handleDisconnect">
          Cancel
        </button>
      </template>

      <!-- Connected state -->
      <template v-else>
        <span class="status-badge connected">
          <span class="status-dot" />
          Connected
        </span>
        <span class="device-name">{{ ip }}</span>
        <button class="btn btn-danger" @click="handleDisconnect">
          <WifiOff :size="14" class="btn-icon" />
          Disconnect
        </button>
      </template>
    </div>

    <p v-if="!isSupported" class="warning">
      <AlertTriangle :size="14" class="warning-icon" />
      HTTP is not supported in this browser. Please use a modern browser
      such as <strong>Chrome</strong>, <strong>Edge</strong>, or <strong>Firefox</strong>.
    </p>

    <p v-if="isSupported && errorMsg" class="error">
      {{ errorMsg }}
    </p>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { Wifi, WifiOff, AlertTriangle } from 'lucide-vue-next'
import { useTelemetry } from '../composables/useTelemetry.js'

const ws = useTelemetry()

const state = ref('disconnected')
const ip = ref('')
const ipInput = ref('')
const isSupported = ref(true)
const errorMsg = ref('')
const isAlerting = ref(false)
let alertTimeout = null

// Check HTTP fetch support
onMounted(() => {
  isSupported.value = typeof window !== 'undefined' && 'fetch' in window
})

// Listen to state changes
const unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ws.manager.on('stateChange', (newState) => {
      state.value = newState
      errorMsg.value = ''
    })
  )

  // Alert state: go into alerting mode when anomaly detected, return after 5s
  unsubscribers.push(
    ws.manager.on('alert', ({ alert }) => {
      if (alert) {
        isAlerting.value = true
        if (alertTimeout) clearTimeout(alertTimeout)
        alertTimeout = setTimeout(() => {
          isAlerting.value = false
          alertTimeout = null
        }, 5000)
      }
    })
  )

  unsubscribers.push(
    ws.manager.on('error', ({ message }) => {
      errorMsg.value = message || 'Connection failed'
    })
  )
})

onUnmounted(() => {
  unsubscribers.forEach((unsub) => unsub())
  if (alertTimeout) clearTimeout(alertTimeout)
})

function handleConnect() {
  const trimmedIp = ipInput.value.trim()
  if (!trimmedIp) return
  ip.value = trimmedIp
  ws.connect(trimmedIp)
}

function handleDisconnect() {
  ws.disconnect()
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
  gap: 0.75rem;
  flex-wrap: wrap;
}

/* IP Input Group */
.ip-input-group {
  display: inline-flex;
  align-items: center;
  background: var(--bg-secondary);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  padding: 0 0.6rem;
  transition: border-color var(--transition-fast);
  max-width: 220px;
  width: 100%;
}

.ip-input-group:focus-within {
  border-color: var(--accent);
}

.ip-icon {
  color: var(--text-muted);
  flex-shrink: 0;
}

.ip-input {
  background: transparent;
  border: none;
  outline: none;
  color: var(--text-primary);
  font-size: 0.85rem;
  font-family: var(--mono);
  padding: 0.45rem 0.4rem;
  width: 100%;
  min-width: 0;
}

.ip-input::placeholder {
  color: var(--text-muted);
  opacity: 0.6;
}

.ip-port {
  color: var(--text-muted);
  font-size: 0.78rem;
  font-family: var(--mono);
  flex-shrink: 0;
}

/* Buttons */
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
  white-space: nowrap;
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

.btn-danger {
  background: rgba(255, 77, 106, 0.1);
  color: var(--color-danger);
  border: 1px solid rgba(255, 77, 106, 0.2);
}

.btn-danger:hover:not(:disabled) {
  background: rgba(255, 77, 106, 0.18);
}

.btn-ghost {
  background: transparent;
  color: var(--text-secondary);
  border: 1px solid var(--border);
}

.btn-ghost:hover:not(:disabled) {
  border-color: var(--border-hover);
  color: var(--text-primary);
}

.btn-icon {
  flex-shrink: 0;
}

/* Status badge */
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

.status-badge.connected {
  background: rgba(0, 229, 160, 0.1);
  color: var(--color-success);
}

.status-badge.connected .status-dot {
  background: var(--color-success);
  box-shadow: 0 0 6px var(--color-success);
  animation: pulse-green 2s infinite;
}

/* Device name */
.device-name {
  color: var(--text-secondary);
  font-size: 0.82rem;
  font-family: var(--mono);
}

/* Warnings */
.warning {
  margin-top: 0.65rem;
  padding: 0.55rem 0.75rem;
  background: rgba(255, 176, 32, 0.08);
  border-left: 3px solid var(--color-warning);
  border-radius: 4px;
  color: #FFB020;
  font-size: 0.8rem;
  line-height: 1.4;
  display: flex;
  align-items: flex-start;
  gap: 0.5rem;
}

.warning-icon {
  flex-shrink: 0;
  margin-top: 1px;
  color: var(--color-warning);
}

.error {
  margin-top: 0.5rem;
  padding: 0.45rem 0.7rem;
  background: rgba(255, 77, 106, 0.06);
  border-left: 3px solid var(--color-danger);
  border-radius: 4px;
  color: #FF4D6A;
  font-size: 0.78rem;
  font-family: var(--mono);
}

/* Spinner */
.spinner {
  display: inline-block;
  width: 14px;
  height: 14px;
  border: 2px solid rgba(255, 255, 255, 0.2);
  border-top-color: #fff;
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

@container (max-width: 500px) {
  .conn-panel {
    gap: 0.3rem;
    flex-wrap: wrap;
  }
  .ip-input-group {
    max-width: none;
    flex: 1;
    min-width: 0;
  }
  .btn {
    padding: 0.4rem 0.6rem;
    font-size: 0.72rem;
  }
  .status-badge {
    font-size: 0.65rem;
    padding: 0.2rem 0.5rem;
  }
  .device-name {
    font-size: 0.7rem;
  }
}
</style>
