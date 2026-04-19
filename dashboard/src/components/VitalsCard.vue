<template>
  <div class="vitals-card" :class="colorClass">
    <div class="card-top-bar" />
    <div class="card-header">
      <div class="card-icon-wrap">
        <Heart v-if="iconName === 'Heart'" :size="18" />
        <Droplets v-else-if="iconName === 'Droplets'" :size="18" />
        <Zap v-else-if="iconName === 'Zap'" :size="18" />
        <Activity v-else :size="18" />
      </div>
      <span class="card-label">{{ label }}</span>
    </div>
    <div class="card-value-row">
      <span class="card-value">{{ displayValue }}</span>
      <span class="card-unit">{{ unit }}</span>
    </div>
    <div v-if="showTrend" class="card-trend" :class="trendDirection">
      <TrendingUp v-if="trendValue >= 0" :size="14" />
      <TrendingDown v-else :size="14" />
      {{ Math.abs(trendValue).toFixed(1) }}
    </div>
  </div>
</template>

<script setup>
import { computed, ref, watch, onUnmounted } from 'vue'
import {
  Heart,
  Droplets,
  Zap,
  Activity,
  TrendingUp,
  TrendingDown,
} from 'lucide-vue-next'

const props = defineProps({
  label: { type: String, required: true },
  value: { type: Number, default: 0 },
  unit: { type: String, default: '' },
  color: { type: String, default: 'info', validator: (v) => ['danger', 'purple', 'info', 'success'].includes(v) },
  iconName: { type: String, default: 'Heart' },
  decimals: { type: Number, default: 1 },
})

const colorClass = computed(() => `card-${props.color}`)

const displayValue = computed(() => {
  if (props.value === null || props.value === undefined) return '--'
  return props.value.toFixed(props.decimals)
})

// Simple trend tracking
const trendValue = ref(0)
const showTrend = ref(false)
let trendTimer = null

watch(
  () => props.value,
  (newVal, oldVal) => {
    if (oldVal !== null && oldVal !== undefined) {
      trendValue.value = newVal - oldVal
      showTrend.value = true
      if (trendTimer) clearTimeout(trendTimer)
      trendTimer = setTimeout(() => {
        showTrend.value = false
        trendTimer = null
      }, 2000)
    }
  }
)

onUnmounted(() => {
  if (trendTimer) clearTimeout(trendTimer)
})

const trendDirection = computed(() =>
  trendValue.value >= 0 ? 'trend-up' : 'trend-down'
)
</script>

<style scoped>
.vitals-card {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 1.1rem 1.25rem 1rem;
  position: relative;
  overflow: hidden;
  transition: all var(--transition-normal);
  min-width: 140px;
}

.vitals-card:hover {
  border-color: var(--border-hover);
  background: var(--card-bg-hover);
}

/* Top accent bar */
.card-top-bar {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  height: 3px;
  opacity: 0.8;
}

.card-danger .card-top-bar { background: linear-gradient(90deg, var(--color-danger), rgba(232, 93, 74, 0.3)); }
.card-purple .card-top-bar { background: linear-gradient(90deg, var(--color-purple), rgba(176, 138, 219, 0.3)); }
.card-info .card-top-bar { background: linear-gradient(90deg, var(--color-info), rgba(90, 158, 199, 0.3)); }
.card-success .card-top-bar { background: linear-gradient(90deg, var(--color-success), rgba(90, 184, 143, 0.3)); }

.vitals-card:hover .card-top-bar {
  opacity: 1;
}

.card-header {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 0.6rem;
}

.card-icon-wrap {
  width: 30px;
  height: 30px;
  border-radius: var(--radius-sm);
  display: flex;
  align-items: center;
  justify-content: center;
}

.card-danger .card-icon-wrap { background: rgba(232, 93, 74, 0.12); color: var(--color-danger); }
.card-purple .card-icon-wrap { background: rgba(176, 138, 219, 0.12); color: var(--color-purple); }
.card-info .card-icon-wrap { background: rgba(90, 158, 199, 0.12); color: var(--color-info); }
.card-success .card-icon-wrap { background: rgba(90, 184, 143, 0.12); color: var(--color-success); }

.card-label {
  font-size: 0.75rem;
  font-weight: 500;
  color: var(--text-muted);
  text-transform: uppercase;
  letter-spacing: 0.8px;
}

.card-value-row {
  display: flex;
  align-items: baseline;
  gap: 0.35rem;
}

.card-value {
  font-size: 1.85rem;
  font-weight: 700;
  font-family: var(--mono);
  line-height: 1.1;
}

.card-danger .card-value { color: #f0a09a; }
.card-purple .card-value { color: #d0c4f0; }
.card-info .card-value { color: #9ecce0; }
.card-success .card-value { color: #9dd8be; }

.card-unit {
  font-size: 0.82rem;
  font-weight: 500;
  color: var(--text-muted);
}

.card-trend {
  margin-top: 0.35rem;
  font-size: 0.72rem;
  font-family: var(--mono);
  display: flex;
  align-items: center;
  gap: 0.2rem;
}

.trend-up {
  color: var(--color-danger);
}

.trend-down {
  color: var(--color-success);
}
</style>
