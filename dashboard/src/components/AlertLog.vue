<template>
  <div class="alert-log">
    <div class="alert-header">
      <div class="alert-header-left">
        <h3 class="alert-title">
          Alert Log
          <span v-if="alerts.length > 0" class="alert-count">{{ alerts.length }}</span>
        </h3>
      </div>
      <div class="alert-header-right">
        <button
          v-if="alerts.length > 0"
          class="btn-export"
          @click="exportAlerts"
          title="Export alerts as CSV"
        >
          <Download :size="14" />
          Export
        </button>
        <button
          v-if="alerts.length > 0"
          class="btn-clear"
          @click="clearAlerts"
          title="Clear alert log"
        >
          Clear
        </button>
      </div>
    </div>

    <div ref="logContainer" class="alert-list">
      <div v-if="alerts.length === 0" class="alert-empty">
        <ShieldCheck :size="28" class="empty-icon" />
        <p>No alerts recorded</p>
        <p class="hint">Alerts will appear here when stress anomalies are detected</p>
      </div>

      <TransitionGroup name="alert-slide">
        <div
          v-for="alert in alerts"
          :key="alert.id"
          class="alert-entry"
        >
          <span class="alert-time">
            <Clock :size="12" class="time-icon" />
            {{ alert.time }}
          </span>
          <span class="alert-message">
            <AlertTriangle :size="14" class="alert-icon-inline" />
            Stress detected
          </span>
          <span class="alert-details">
            HR: {{ alert.heartRate.toFixed(0) }} BPM &middot;
            EMG: {{ alert.emgEnvelope.toFixed(1) }} &middot;
            Motion: {{ alert.motionMagnitude.toFixed(2) }}<template v-if="alert.mse != null"> &middot; MSE: {{ alert.mse.toFixed(6) }}</template>
          </span>
        </div>
      </TransitionGroup>
    </div>
  </div>
</template>

<script setup>
import { ref, nextTick, onMounted, onUnmounted } from 'vue'
import { AlertTriangle, Clock, ShieldCheck, Download } from 'lucide-vue-next'
import { useTelemetry } from '../composables/useTelemetry.js'

const ws = useTelemetry()

const alerts = ref([])
const logContainer = ref(null)
let alertCounter = 0
let unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ws.manager.on('alert', ({ alert, hr, emg, mot, mse }) => {
      if (!alert) return

      const now = new Date()
      const time = now.toLocaleTimeString('en-US', { hour12: false })

      alerts.value.push({
        id: ++alertCounter,
        time,
        heartRate: hr,
        emgEnvelope: emg,
        motionMagnitude: mot,
        mse: mse ?? null,
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

function exportAlerts() {
  const header = 'Time,Heart Rate (BPM),EMG Envelope (uV),Motion (g),MSE'
  const rows = alerts.value.map(a =>
    [a.time, a.heartRate.toFixed(1), a.emgEnvelope.toFixed(2), a.motionMagnitude.toFixed(4), a.mse != null ? a.mse.toFixed(6) : ''].join(',')
  )
  const csv = [header, ...rows].join('\n')
  const blob = new Blob([csv], { type: 'text/csv' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `senseware_alerts_${new Date().toISOString().slice(0, 19).replace(/:/g, '-')}.csv`
  a.click()
  URL.revokeObjectURL(url)
}
</script>

<style scoped>
.alert-log {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
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
  background: rgba(255, 77, 106, 0.12);
  color: var(--color-danger);
  font-size: 0.72rem;
  font-weight: 700;
}

.alert-header-right {
  display: flex;
  gap: 0.4rem;
}

.btn-export {
  padding: 0.3rem 0.6rem;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: transparent;
  color: var(--text-secondary);
  font-size: 0.78rem;
  cursor: pointer;
  display: flex;
  align-items: center;
  gap: 0.3rem;
  transition: all var(--transition-fast);
}

.btn-export:hover {
  background: rgba(139, 92, 246, 0.12);
  color: var(--accent);
  border-color: var(--accent);
}

.btn-clear {
  padding: 0.3rem 0.75rem;
  border: 1px solid var(--border);
  border-radius: 6px;
  background: transparent;
  color: var(--text-secondary);
  font-size: 0.78rem;
  cursor: pointer;
  transition: all var(--transition-fast);
}

.btn-clear:hover {
  background: rgba(255, 255, 255, 0.08);
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
  color: var(--color-success);
  margin-bottom: 0.5rem;
  opacity: 0.6;
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
  border-radius: var(--radius-sm);
  background: rgba(255, 77, 106, 0.06);
  border-left: 3px solid var(--color-danger);
  display: flex;
  flex-direction: column;
  gap: 0.2rem;
}

.alert-time {
  font-size: 0.72rem;
  font-family: var(--mono);
  color: var(--text-muted);
  display: flex;
  align-items: center;
  gap: 0.3rem;
}

.time-icon {
  opacity: 0.6;
}

.alert-message {
  font-size: 0.85rem;
  font-weight: 600;
  color: #FF4D6A;
  display: flex;
  align-items: center;
  gap: 0.3rem;
}

.alert-icon-inline {
  color: var(--color-warning);
  flex-shrink: 0;
}

.alert-details {
  font-size: 0.72rem;
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

@container (max-width: 500px) {
  .alert-log {
    padding: 0.75rem;
    max-height: 280px;
    border-radius: var(--radius-sm);
  }
  .alert-title {
    font-size: 0.8rem;
  }
  .alert-entry {
    padding: 0.4rem 0.5rem;
    margin-bottom: 0.3rem;
  }
  .alert-details {
    font-size: 0.65rem;
  }
  .btn-export,
  .btn-clear {
    font-size: 0.7rem;
    padding: 0.2rem 0.5rem;
  }
}
</style>
