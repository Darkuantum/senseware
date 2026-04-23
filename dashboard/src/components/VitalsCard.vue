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
  min-width: 0;
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

.card-danger .card-top-bar { background: linear-gradient(90deg, #FF4D6A, rgba(255, 77, 106, 0.2)); }
.card-purple .card-top-bar { background: linear-gradient(90deg, #9D4EDD, rgba(157, 78, 221, 0.2)); }
.card-info .card-top-bar { background: linear-gradient(90deg, #00E5FF, rgba(0, 229, 255, 0.2)); }
.card-success .card-top-bar { background: linear-gradient(90deg, #00E5A0, rgba(0, 229, 160, 0.2)); }

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

.card-danger .card-icon-wrap { background: rgba(255, 77, 106, 0.12); color: #FF4D6A; }
.card-purple .card-icon-wrap { background: rgba(157, 78, 221, 0.12); color: #9D4EDD; }
.card-info .card-icon-wrap { background: rgba(0, 229, 255, 0.1); color: #00E5FF; }
.card-success .card-icon-wrap { background: rgba(0, 229, 160, 0.1); color: #00E5A0; }

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

.card-danger .card-value { color: #FF4D6A; }
.card-purple .card-value { color: #9D4EDD; }
.card-info .card-value { color: #00E5FF; }
.card-success .card-value { color: #00E5A0; }

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

@container (max-width: 500px) {
  .vitals-card {
    min-width: 0 !important;
    padding: 0.5rem 0.6rem;
  }
  .card-icon-wrap {
    width: 30px;
    height: 30px;
  }
  .card-icon-wrap svg {
    width: 14px;
    height: 14px;
  }
  .card-label {
    font-size: 0.65rem;
  }
  .card-value {
    font-size: 1rem;
  }
  .card-unit {
    font-size: 0.6rem;
  }
  .card-trend {
    font-size: 0.6rem;
  }
}
</style>
