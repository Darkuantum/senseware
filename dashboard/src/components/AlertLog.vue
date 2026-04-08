<template>
  <div class="alert-log">
    <div class="alert-header">
      <div class="alert-header-left">
        <h3 class="alert-title">
          Alert Log
          <span v-if="alerts.length > 0" class="alert-count">{{ alerts.length }}</span>
        </h3>
      </div>
      <button
        v-if="alerts.length > 0"
        class="btn-clear"
        @click="clearAlerts"
        title="Clear alert log"
      >
        Clear
      </button>
    </div>

    <div ref="logContainer" class="alert-list">
      <div v-if="alerts.length === 0" class="alert-empty">
        <span class="empty-icon">&#x2705;</span>
        <p>No alerts recorded</p>
        <p class="hint">Alerts will appear here when stress anomalies are detected</p>
      </div>

      <TransitionGroup name="alert-slide">
        <div
          v-for="alert in alerts"
          :key="alert.id"
          class="alert-entry"
        >
          <span class="alert-time">{{ alert.time }}</span>
          <span class="alert-message">
            &#x26A0;&#xFE0F; Stress detected
          </span>
          <span class="alert-details">
            HR: {{ alert.heartRate.toFixed(0) }} BPM &middot;
            EMG: {{ alert.emgEnvelope.toFixed(1) }} &middot;
            Motion: {{ alert.motionMagnitude.toFixed(2) }}
          </span>
        </div>
      </TransitionGroup>
    </div>
  </div>
</template>

<script setup>
import { ref, nextTick, onMounted, onUnmounted } from 'vue'
import { useBluetooth } from '../composables/useBluetooth.js'

const ble = useBluetooth()

const alerts = ref([])
const logContainer = ref(null)
let alertCounter = 0
let unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ble.on('alert', ({ anomaly, timestamp, heartRate, emgEnvelope, motionMagnitude }) => {
      if (!anomaly) return

      const now = new Date(timestamp)
      const time = now.toLocaleTimeString('en-US', { hour12: false })

      alerts.value.push({
        id: ++alertCounter,
        time,
        heartRate,
        emgEnvelope,
        motionMagnitude,
      })

      // Keep only the last 200 alerts
      if (alerts.value.length > 200) {
        alerts.value.splice(0, alerts.value.length - 200)
      }

      // Auto-scroll to bottom
      nextTick(() => {
        if (logContainer.value) {
          logContainer.value.scrollTop = logContainer.value.scrollHeight
        }
      })
    })
  )
})

onUnmounted(() => {
  unsubscribers.forEach((u) => u())
})

function clearAlerts() {
  alerts.value = []
}
</script>

<style scoped>
.alert-log {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 1rem 1.25rem;
  display: flex;
  flex-direction: column;
  max-height: 400px;
}

.alert-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 0.75rem;
  flex-shrink: 0;
}

.alert-title {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.alert-count {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  min-width: 22px;
  height: 22px;
  padding: 0 6px;
  border-radius: 11px;
  background: rgba(239, 68, 68, 0.2);
  color: #ef4444;
  font-size: 0.72rem;
  font-weight: 700;
}

.btn-clear {
  padding: 0.3rem 0.75rem;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: transparent;
  color: var(--text-secondary);
  font-size: 0.78rem;
  cursor: pointer;
  transition: all 0.2s ease;
}

.btn-clear:hover {
  background: var(--border);
  color: var(--text-primary);
}

.alert-list {
  overflow-y: auto;
  flex: 1;
  scrollbar-width: thin;
  scrollbar-color: var(--border) transparent;
}

.alert-list::-webkit-scrollbar {
  width: 4px;
}

.alert-list::-webkit-scrollbar-track {
  background: transparent;
}

.alert-list::-webkit-scrollbar-thumb {
  background: var(--border);
  border-radius: 2px;
}

.alert-empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 2rem 0;
  color: var(--text-muted);
}

.empty-icon {
  font-size: 1.5rem;
  margin-bottom: 0.5rem;
}

.alert-empty p {
  margin: 0.15rem 0;
  font-size: 0.85rem;
}

.alert-empty .hint {
  font-size: 0.75rem;
  color: var(--text-secondary);
}

.alert-entry {
  padding: 0.6rem 0.75rem;
  margin-bottom: 0.4rem;
  border-radius: 8px;
  background: rgba(239, 68, 68, 0.06);
  border-left: 3px solid #ef4444;
  display: flex;
  flex-direction: column;
  gap: 0.2rem;
}

.alert-time {
  font-size: 0.75rem;
  font-family: var(--mono);
  color: var(--text-muted);
}

.alert-message {
  font-size: 0.85rem;
  font-weight: 600;
  color: #fca5a5;
}

.alert-details {
  font-size: 0.75rem;
  font-family: var(--mono);
  color: var(--text-secondary);
}

/* Transition animations */
.alert-slide-enter-active {
  transition: all 0.3s ease-out;
}

.alert-slide-enter-from {
  opacity: 0;
  transform: translateX(-20px);
}
</style>
