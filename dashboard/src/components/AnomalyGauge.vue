<template>
  <div class="anomaly-gauge" :class="{ 'anomaly-active': anomaly }">
    <div class="gauge-header">
      <div class="gauge-title">
        <Brain :size="16" />
        <span>Model Status</span>
      </div>
      <div class="gauge-status" :class="anomaly ? 'status-danger' : 'status-normal'">
        <span class="status-dot" :class="{ pulsing: anomaly }" />
        <span v-if="anomaly" class="status-text">ANOMALY DETECTED</span>
        <span v-else class="status-text">Normal</span>
        <span v-if="anomaly && anomalySecondsAgo !== null" class="status-time">
          {{ anomalySecondsAgo }}s ago
        </span>
      </div>
    </div>

    <div class="progress-track">
      <div
        class="progress-fill"
        :style="{ width: `${Math.max(0, Math.min(100, accuracy))}%`, background: accuracyColor }"
      />
    </div>

    <div class="stats-row">
      <span class="stat-mono">MSE: {{ mse.toFixed(5) }}</span>
      <span class="stat-mono">Accuracy: {{ accuracy.toFixed(1) }}%</span>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { Brain } from 'lucide-vue-next'
import { useTelemetry } from '../composables/useTelemetry.js'

const { manager } = useTelemetry()

const mse = ref(0)
const accuracy = ref(100)
const anomaly = ref(false)
const anomalyTimestamp = ref(null)
const anomalySecondsAgo = ref(null)

let countdownTimer = null
let anomalyTimeout = null
const unsubscribers = []

const accuracyColor = computed(() => {
  if (accuracy.value > 80) return '#00E5A0'
  if (accuracy.value >= 40) return '#FFB020'
  return '#FF4D6A'
})

function resetGauge() {
  mse.value = 0
  accuracy.value = 100
  anomaly.value = false
  anomalyTimestamp.value = null
  anomalySecondsAgo.value = null
  if (anomalyTimeout) {
    clearTimeout(anomalyTimeout)
    anomalyTimeout = null
  }
}

onMounted(() => {
  unsubscribers.push(manager.on('telemetry', (data) => {
    mse.value = data.mse ?? 0
    accuracy.value = data.acc ?? 100
    const isAnomaly = data.anomaly === 1 || data.anomaly === true
    if (isAnomaly && !anomaly.value) {
      anomaly.value = true
      anomalyTimestamp.value = Date.now()
      if (anomalyTimeout) clearTimeout(anomalyTimeout)
      anomalyTimeout = setTimeout(() => {
        anomaly.value = false
        anomalyTimestamp.value = null
        anomalySecondsAgo.value = null
        anomalyTimeout = null
      }, 15000)
    }
  }))

  unsubscribers.push(manager.on('stateChange', (state) => {
    if (state === 'disconnected') resetGauge()
  }))

  countdownTimer = setInterval(() => {
    if (anomaly.value && anomalyTimestamp.value !== null) {
      anomalySecondsAgo.value = Math.floor((Date.now() - anomalyTimestamp.value) / 1000)
    }
  }, 1000)
})

onUnmounted(() => {
  unsubscribers.forEach((u) => u())
  if (countdownTimer) clearInterval(countdownTimer)
  if (anomalyTimeout) clearTimeout(anomalyTimeout)
})
</script>

<style scoped>
.anomaly-gauge {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 1rem 1.25rem;
  transition: border-color 0.3s ease, box-shadow 0.3s ease;
}

.anomaly-gauge.anomaly-active {
  border-color: rgba(255, 77, 106, 0.4);
  box-shadow: 0 0 20px rgba(255, 77, 106, 0.1);
}

.gauge-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 0.75rem;
}

.gauge-title {
  display: flex;
  align-items: center;
  gap: 0.4rem;
  font-size: 0.8rem;
  font-weight: 600;
  color: var(--text-primary);
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

.gauge-status {
  display: flex;
  align-items: center;
  gap: 0.4rem;
}

.status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  flex-shrink: 0;
}

.status-normal .status-dot {
  background: #00E5A0;
}

.status-danger .status-dot {
  background: #FF4D6A;
  animation: pulse-dot 1s ease-in-out infinite;
}

@keyframes pulse-dot {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.5; transform: scale(1.4); }
}

.status-text {
  font-size: 0.7rem;
  font-weight: 600;
  letter-spacing: 0.5px;
}

.status-normal .status-text {
  color: #00E5A0;
}

.status-danger .status-text {
  color: #FF4D6A;
}

.status-time {
  font-size: 0.65rem;
  font-family: var(--mono);
  color: rgba(255, 77, 106, 0.7);
  margin-left: 0.2rem;
}

.progress-track {
  width: 100%;
  height: 6px;
  background: var(--border);
  border-radius: 3px;
  overflow: hidden;
  margin-bottom: 0.6rem;
}

.progress-fill {
  height: 100%;
  border-radius: 3px;
  transition: width 0.4s ease;
}

.stats-row {
  display: flex;
  justify-content: space-between;
}

.stat-mono {
  font-size: 0.75rem;
  font-family: var(--mono);
  color: var(--text-muted);
}

@container (max-width: 500px) {
  .anomaly-gauge {
    padding: 0.75rem;
    border-radius: var(--radius-sm);
  }
  .gauge-header {
    flex-wrap: wrap;
    gap: 0.4rem;
  }
  .gauge-title {
    font-size: 0.7rem;
  }
  .status-text {
    font-size: 0.6rem;
  }
  .stat-mono {
    font-size: 0.65rem;
  }
}
</style>
