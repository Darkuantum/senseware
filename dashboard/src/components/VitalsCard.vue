<template>
  <div class="vitals-card" :class="colorClass">
    <div class="card-header">
      <span class="card-indicator" />
      <span class="card-label">{{ label }}</span>
    </div>
    <div class="card-value-row">
      <span class="card-value">{{ displayValue }}</span>
      <span class="card-unit">{{ unit }}</span>
    </div>
    <div v-if="showTrend" class="card-trend" :class="trendDirection">
      {{ trendArrow }} {{ Math.abs(trendValue).toFixed(1) }}
    </div>
  </div>
</template>

<script setup>
import { computed, ref, watch } from 'vue'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: Number, default: 0 },
  unit: { type: String, default: '' },
  color: { type: String, default: 'blue', validator: (v) => ['red', 'blue', 'green'].includes(v) },
  decimals: { type: Number, default: 1 },
})

const colorClass = computed(() => `card-${props.color}`)

const displayValue = computed(() => {
  if (props.value === null || props.value === undefined) return '--'
  return props.value.toFixed(props.decimals)
})

// Simple trend tracking
const previousValue = ref(props.value)
const trendValue = ref(0)
const showTrend = ref(false)

watch(
  () => props.value,
  (newVal, oldVal) => {
    if (oldVal !== null && oldVal !== undefined) {
      trendValue.value = newVal - oldVal
      showTrend.value = true
      // Hide trend after 2s
      setTimeout(() => {
        showTrend.value = false
      }, 2000)
    }
    previousValue.value = newVal
  }
)

const trendDirection = computed(() =>
  trendValue.value >= 0 ? 'trend-up' : 'trend-down'
)

const trendArrow = computed(() =>
  trendValue.value >= 0 ? '↑' : '↓'
)
</script>

<style scoped>
.vitals-card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: 12px;
  padding: 1rem 1.25rem;
  min-width: 140px;
  transition: border-color 0.3s ease;
}

.vitals-card:hover {
  border-color: var(--border-hover);
}

.card-header {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 0.5rem;
}

.card-indicator {
  width: 10px;
  height: 10px;
  border-radius: 50%;
}

.card-red .card-indicator {
  background: #ef4444;
  box-shadow: 0 0 6px rgba(239, 68, 68, 0.5);
}

.card-blue .card-indicator {
  background: #3b82f6;
  box-shadow: 0 0 6px rgba(59, 130, 246, 0.5);
}

.card-green .card-indicator {
  background: #22c55e;
  box-shadow: 0 0 6px rgba(34, 197, 94, 0.5);
}

.card-label {
  font-size: 0.78rem;
  font-weight: 500;
  color: var(--text-secondary);
  text-transform: uppercase;
  letter-spacing: 0.8px;
}

.card-value-row {
  display: flex;
  align-items: baseline;
  gap: 0.35rem;
}

.card-value {
  font-size: 2rem;
  font-weight: 700;
  font-family: var(--mono);
  color: var(--text-primary);
  line-height: 1.1;
}

.card-red .card-value {
  color: #fca5a5;
}

.card-blue .card-value {
  color: #93c5fd;
}

.card-green .card-value {
  color: #86efac;
}

.card-unit {
  font-size: 0.85rem;
  font-weight: 500;
  color: var(--text-muted);
}

.card-trend {
  margin-top: 0.3rem;
  font-size: 0.75rem;
  font-family: var(--mono);
}

.trend-up {
  color: #ef4444;
}

.trend-down {
  color: #22c55e;
}
</style>
