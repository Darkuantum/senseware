<template>
  <div class="chart-container">
    <div class="chart-header">
      <div class="chart-header-left">
        <h3 class="chart-title">Live Telemetry</h3>
        <span v-if="dataPointCount > 0" class="chart-subtitle">
          {{ dataPointCount }} samples
        </span>
      </div>
      <div class="chart-header-right">
        <div class="time-range-btns">
          <button
            v-for="opt in timeOptions"
            :key="opt.value"
            :class="['range-btn', { active: maxPoints === opt.value }]"
            @click="setMaxPoints(opt.value)"
          >{{ opt.label }}</button>
        </div>
      </div>
    </div>
    <div class="chart-wrapper">
      <Line v-if="chartMounted" :data="chartData" :options="chartOptions" />
      <div v-if="!hasData" class="chart-placeholder">
        <Activity :size="32" class="placeholder-icon" />
        <p>Waiting for telemetry data...</p>
        <p class="hint">Connect to the Senseware device to see live data</p>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted } from 'vue'
import { Activity } from 'lucide-vue-next'
import { Line } from 'vue-chartjs'
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Filler,
  Tooltip,
  Legend,
} from 'chart.js'
import { useTelemetry } from '../composables/useTelemetry.js'

// Register Chart.js components
ChartJS.register(
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Filler,
  Tooltip,
  Legend
)

const ws = useTelemetry()

const chartMounted = ref(false)

const MAX_POINTS = 60
const maxPoints = ref(60)
const timeOptions = [
  { label: '30s', value: 30 },
  { label: '1m', value: 60 },
  { label: '2m', value: 120 },
  { label: '5m', value: 300 },
]

// Plain (non-reactive) arrays — avoid Chart.js reactivity loop
const _labels = []
const _hrData = []
const _spo2Data = []
const _emgData = []
const _motionData = []
const dataVersion = ref(0)

// Pre-fill arrays for fixed-width rolling graph
for (let i = 0; i < MAX_POINTS; i++) {
  _labels.push('')
  _hrData.push(null)
  _spo2Data.push(null)
  _emgData.push(null)
  _motionData.push(null)
}

function pushDataPoint(data) {
  const now = new Date()
  const timeLabel = now.toLocaleTimeString('en-US', { hour12: false })
  _labels.push(timeLabel)
  _hrData.push(data.hr)
  _spo2Data.push(data.spo2)
  _emgData.push(data.emg)
  _motionData.push(data.mot)
  while (_labels.length > maxPoints.value) {
    _labels.shift()
    _hrData.shift()
    _spo2Data.shift()
    _emgData.shift()
    _motionData.shift()
  }
  dataVersion.value++
}

function setMaxPoints(val) {
  maxPoints.value = val
  while (_labels.length > val) {
    _labels.shift()
    _hrData.shift()
    _spo2Data.shift()
    _emgData.shift()
    _motionData.shift()
  }
  dataVersion.value++
}

const dataPointCount = computed(() => { void dataVersion.value; return _hrData.filter(v => v !== null).length })
const hasData = computed(() => { void dataVersion.value; return _hrData.some(v => v !== null) })

// Unsubscribers for telemetry events
let unsubscribers = []

onMounted(() => {
  chartMounted.value = true

  unsubscribers.push(
    ws.manager.on('telemetry', (data) => {
      pushDataPoint(data)
    })
  )

  // Clear chart data on disconnect
  unsubscribers.push(
    ws.manager.on('stateChange', (newState) => {
      if (newState === 'disconnected') {
        _labels.length = 0
        _hrData.length = 0
        _spo2Data.length = 0
        _emgData.length = 0
        _motionData.length = 0
        // Re-pre-fill
        for (let i = 0; i < maxPoints.value; i++) {
          _labels.push('')
          _hrData.push(null)
          _spo2Data.push(null)
          _emgData.push(null)
          _motionData.push(null)
        }
        dataVersion.value++
      }
    })
  )
})

onUnmounted(() => {
  unsubscribers.forEach((u) => u())
})

const chartData = computed(() => {
  void dataVersion.value // Force re-computation when dataVersion changes
  return {
    labels: [..._labels],
    datasets: [
      {
        label: 'Heart Rate (BPM)',
        data: [..._hrData],
        borderColor: '#FF4D6A',
        backgroundColor: 'rgba(255, 77, 106, 0.15)',
        fill: true,
        tension: 0.4,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        yAxisID: 'y',
        spanGaps: true,
      },
      {
        label: 'SpO2 (%)',
        data: [..._spo2Data],
        borderColor: '#9D4EDD',
        backgroundColor: 'rgba(157, 78, 221, 0.15)',
        fill: true,
        tension: 0.4,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        yAxisID: 'y',
        spanGaps: true,
      },
      {
        label: 'EMG Envelope (μV)',
        data: [..._emgData],
        borderColor: '#00E5FF',
        backgroundColor: 'rgba(0, 229, 255, 0.15)',
        fill: true,
        tension: 0.4,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        yAxisID: 'y1',
        spanGaps: true,
      },
      {
        label: 'Motion (g)',
        data: [..._motionData],
        borderColor: '#00E5A0',
        backgroundColor: 'rgba(0, 229, 160, 0.15)',
        fill: true,
        tension: 0.4,
        borderWidth: 2,
        pointRadius: 0,
        pointHoverRadius: 4,
        yAxisID: 'y2',
        spanGaps: true,
      },
    ],
  }
})

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  animation: {
    duration: 200,
    easing: 'linear',
  },
  interaction: {
    mode: 'index',
    intersect: false,
  },
  plugins: {
    legend: {
      display: true,
      position: 'bottom',
      labels: {
        color: '#B0B0C8',
        font: { size: 11 },
        usePointStyle: true,
        pointStyle: 'circle',
        padding: 16,
      },
    },
    tooltip: {
      backgroundColor: 'rgba(26, 22, 37, 0.95)',
      titleColor: '#FFFFFF',
      bodyColor: '#B0B0C8',
      borderColor: 'rgba(255, 255, 255, 0.1)',
      borderWidth: 1,
      cornerRadius: 8,
      padding: 10,
      bodyFont: { family: 'monospace' },
      callbacks: {
        label: function (context) {
          let label = context.dataset.label || ''
          if (label) label += ': '
          label += context.parsed.y.toFixed(2)
          return label
        },
      },
    },
  },
  scales: {
    x: {
      grid: {
        color: 'rgba(255, 255, 255, 0.06)',
        border: { display: false },
      },
      ticks: {
        color: '#6B6B8A',
        font: { size: 10, family: 'monospace' },
        maxRotation: 0,
        maxTicksLimit: 8,
      },
    },
    y: {
      type: 'linear',
      display: true,
      position: 'left',
      title: {
        display: true,
        text: 'BPM / SpO2',
        color: '#FF4D6A',
        font: { size: 11, weight: '600' },
      },
      grid: {
        color: 'rgba(255, 255, 255, 0.06)',
        border: { display: false },
      },
      ticks: {
        color: '#6B6B8A',
        font: { size: 10, family: 'monospace' },
      },
      min: 40,
      suggestedMax: 120,
    },
    y1: {
      type: 'linear',
      display: true,
      position: 'right',
      title: {
        display: true,
        text: 'EMG (μV)',
        color: '#00E5FF',
        font: { size: 11, weight: '600' },
      },
      grid: {
        drawOnChartArea: false,
      },
      ticks: {
        color: '#6B6B8A',
        font: { size: 10, family: 'monospace' },
      },
    },
    y2: {
      type: 'linear',
      display: false,
      min: 0,
      suggestedMax: 3,
    },
  },
}
</script>

<style scoped>
.chart-container {
  background: var(--card-bg);
  border: 1px solid var(--border);
  border-radius: var(--radius-md);
  padding: 1rem 1.25rem;
}

.chart-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 0.75rem;
}

.chart-header-left {
  display: flex;
  align-items: baseline;
  gap: 0.75rem;
}

.chart-header-right {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.time-range-btns {
  display: flex;
  gap: 0.25rem;
  background: rgba(255, 255, 255, 0.04);
  border-radius: 6px;
  padding: 2px;
}

.range-btn {
  padding: 0.2rem 0.5rem;
  border: none;
  border-radius: 4px;
  background: transparent;
  color: var(--text-muted);
  font-size: 0.7rem;
  font-weight: 600;
  font-family: var(--mono);
  cursor: pointer;
  transition: all var(--transition-fast);
}

.range-btn:hover {
  color: var(--text-secondary);
}

.range-btn.active {
  background: var(--accent);
  color: #fff;
}

.chart-title {
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--text-primary);
  margin: 0;
}

.chart-subtitle {
  font-size: 0.75rem;
  color: var(--text-muted);
  font-family: var(--mono);
}

.chart-wrapper {
  height: 300px;
  position: relative;
}

.chart-placeholder {
  position: absolute;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  pointer-events: none;
}

.placeholder-icon {
  color: var(--text-muted);
  margin-bottom: 0.75rem;
  opacity: 0.6;
}

.chart-placeholder p {
  margin: 0.25rem 0;
  font-size: 0.9rem;
}

.chart-placeholder .hint {
  font-size: 0.78rem;
  color: var(--text-secondary);
}

@container (max-width: 500px) {
  .chart-container {
    padding: 0.75rem;
    border-radius: var(--radius-sm);
  }
  .chart-wrapper {
    height: 220px;
  }
  .chart-title {
    font-size: 0.8rem;
  }
  .chart-subtitle {
    font-size: 0.65rem;
  }
  .chart-header {
    margin-bottom: 0.5rem;
  }
  .time-range-btns {
    gap: 0.15rem;
    padding: 1px;
  }
  .range-btn {
    padding: 0.15rem 0.35rem;
    font-size: 0.6rem;
  }
}
</style>
