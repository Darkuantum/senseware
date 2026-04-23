<template>
  <div class="session-stats">
    <div class="stat-card">
      <Timer :size="14" class="stat-icon" />
      <div class="stat-body">
        <span class="stat-label">Duration</span>
        <span class="stat-value">{{ connectedDuration }}</span>
      </div>
    </div>
    <div class="stat-card">
      <BarChart3 :size="14" class="stat-icon" />
      <div class="stat-body">
        <span class="stat-label">Alerts</span>
        <span class="stat-value">{{ totalAlerts }}</span>
      </div>
    </div>
    <div class="stat-card">
      <TrendingUp :size="14" class="stat-icon" />
      <div class="stat-body">
        <span class="stat-label">Peak HR</span>
        <span class="stat-value">{{ peakHr > 0 ? peakHr.toFixed(0) : '--' }}</span>
      </div>
    </div>
    <div class="stat-card">
      <Activity :size="14" class="stat-icon" />
      <div class="stat-body">
        <span class="stat-label">Avg HR</span>
        <span class="stat-value">{{ hrSamples > 0 ? avgHr.toFixed(0) : '--' }}</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { Timer, BarChart3, TrendingUp, Activity } from 'lucide-vue-next'
import { useTelemetry } from '../composables/useTelemetry.js'

const ws = useTelemetry()

const sessionStart = ref(null)
const totalAlerts = ref(0)
const peakHr = ref(0)
const avgHr = ref(0)
const hrSamples = ref(0)
const durationTrigger = ref(0)
let hrSum = 0
let durationTimer = null
const unsubscribers = []

const connectedDuration = computed(() => {
  void durationTrigger.value
  if (!sessionStart.value) return '--:--'
  const secs = Math.floor((Date.now() - sessionStart.value.getTime()) / 1000)
  const mins = String(Math.floor(secs / 60)).padStart(2, '0')
  const seconds = String(secs % 60).padStart(2, '0')
  return `${mins}:${seconds}`
})

function resetStats() {
  sessionStart.value = null
  totalAlerts.value = 0
  peakHr.value = 0
  avgHr.value = 0
  hrSamples.value = 0
  hrSum = 0
}

onMounted(() => {
  unsubscribers.push(
    ws.manager.on('telemetry', (data) => {
      if (!sessionStart.value) sessionStart.value = new Date()

      if (data.hr !== null && data.hr !== undefined && data.hr > 0) {
        hrSum += data.hr
        hrSamples.value++
        avgHr.value = hrSum / hrSamples.value
        if (data.hr > peakHr.value) peakHr.value = data.hr
      }
    })
  )

  unsubscribers.push(
    ws.manager.on('alert', (data) => {
      if (data.alert) totalAlerts.value++
    })
  )

  unsubscribers.push(
    ws.manager.on('stateChange', (newState) => {
      if (newState === 'disconnected') resetStats()
    })
  )

  durationTimer = setInterval(() => {
    durationTrigger.value++
  }, 1000)
})

onUnmounted(() => {
  unsubscribers.forEach((u) => u())
  if (durationTimer) clearInterval(durationTimer)
})
</script>

<style scoped>
.session-stats {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 0.75rem;
}

.stat-card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-sm);
  padding: 0.75rem 1rem;
  display: flex;
  align-items: flex-start;
  gap: 0.6rem;
}

.stat-icon {
  color: var(--text-muted);
  flex-shrink: 0;
  margin-top: 0.15rem;
}

.stat-body {
  display: flex;
  flex-direction: column;
}

.stat-label {
  font-size: 0.7rem;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 0.25rem;
}

.stat-value {
  font-size: 1.1rem;
  font-weight: 700;
  color: var(--text-primary);
  font-family: var(--mono);
}

@container (max-width: 500px) {
  .session-stats {
    grid-template-columns: repeat(2, 1fr);
    gap: 0.5rem;
  }
  .stat-card {
    padding: 0.5rem 0.6rem;
    gap: 0.4rem;
  }
  .stat-value {
    font-size: 0.9rem;
  }
  .stat-label {
    font-size: 0.6rem;
  }
}
</style>
