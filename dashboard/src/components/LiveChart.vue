<template>
  <div class="chart-container">
    <div class="chart-header">
      <h3 class="chart-title">Live Telemetry</h3>
      <span v-if="dataPointCount > 0" class="chart-subtitle">
        {{ dataPointCount }} samples
      </span>
    </div>
    <div class="chart-wrapper">
      <Line v-if="hasData" :data="chartData" :options="chartOptions" />
      <div v-else class="chart-placeholder">
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
import { useBluetooth } from '../composables/useBluetooth.js'

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

const ble = useBluetooth()

const MAX_POINTS = 60

const hrData = ref([])
const spo2Data = ref([])
const emgData = ref([])
const motionData = ref([])
const labels = ref([])

const dataPointCount = computed(() => hrData.value.length)
const hasData = computed(() => hrData.value.length > 1)

// Unsubscribers for BLE events
let unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ble.on('telemetry', ({ heartRate, spo2, emgEnvelope, motionMagnitude }) => {
      const now = new Date()
      const timeLabel = now.toLocaleTimeString('en-US', { hour12: false })

      labels.value.push(timeLabel)
      hrData.value.push(heartRate)
      spo2Data.value.push(spo2)
      emgData.value.push(emgEnvelope)
      motionData.value.push(motionMagnitude)

      // Decimate to prevent memory growth
      if (labels.value.length > MAX_POINTS) {
        labels.value.shift()
        hrData.value.shift()
        spo2Data.value.shift()
        emgData.value.shift()
        motionData.value.shift()
      }
    })
  )
})

onUnmounted(() => {
  unsubscribers.forEach((u) => u())
})

const chartData = computed(() => ({
  labels: labels.value,
  datasets: [
    {
      label: 'Heart Rate (BPM)',
      data: hrData.value,
      borderColor: '#e85d4a',
      backgroundColor: 'rgba(232, 93, 74, 0.08)',
      fill: true,
      tension: 0.4,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      yAxisID: 'y',
    },
    {
      label: 'SpO2 (%)',
      data: spo2Data.value,
      borderColor: '#b08adb',
      backgroundColor: 'rgba(176, 138, 219, 0.08)',
      fill: true,
      tension: 0.4,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      yAxisID: 'y',
    },
    {
      label: 'EMG Envelope (μV)',
      data: emgData.value,
      borderColor: '#5a9ec7',
      backgroundColor: 'rgba(90, 158, 199, 0.08)',
      fill: true,
      tension: 0.4,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      yAxisID: 'y1',
    },
    {
      label: 'Motion (g)',
      data: motionData.value,
      borderColor: '#5ab88f',
      backgroundColor: 'rgba(90, 184, 143, 0.08)',
      fill: true,
      tension: 0.4,
      borderWidth: 2,
      pointRadius: 0,
      pointHoverRadius: 4,
      yAxisID: 'y1',
    },
  ],
}))

const chartOptions = {
  responsive: true,
  maintainAspectRatio: false,
  animation: {
    duration: 300,
    easing: 'easeOutQuart',
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
        color: '#8a7b6b',
        font: { size: 11 },
        usePointStyle: true,
        pointStyle: 'circle',
        padding: 16,
      },
    },
    tooltip: {
      backgroundColor: 'rgba(26, 22, 20, 0.92)',
      titleColor: '#f5f0eb',
      bodyColor: '#b8a99a',
      borderColor: '#3d342a',
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
        color: 'rgba(61, 52, 42, 0.3)',
        border: { display: false },
      },
      ticks: {
        color: '#8a7b6b',
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
        color: '#e85d4a',
        font: { size: 11, weight: '600' },
      },
      grid: {
        color: 'rgba(61, 52, 42, 0.3)',
        border: { display: false },
      },
      ticks: {
        color: '#8a7b6b',
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
        text: 'EMG / Motion',
        color: '#5a9ec7',
        font: { size: 11, weight: '600' },
      },
      grid: {
        drawOnChartArea: false,
      },
      ticks: {
        color: '#8a7b6b',
        font: { size: 10, family: 'monospace' },
      },
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
  align-items: baseline;
  gap: 0.75rem;
  margin-bottom: 0.75rem;
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
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  height: 100%;
  color: var(--text-muted);
}

.placeholder-icon {
  color: var(--border-hover);
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
</style>
