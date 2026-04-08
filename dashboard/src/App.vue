<template>
  <div class="app-shell">
    <!-- Header -->
    <header class="app-header">
      <div class="header-content">
        <div class="header-brand">
          <span class="brand-icon">&#x1F4E1;</span>
          <div>
            <h1 class="brand-title">Senseware</h1>
            <p class="brand-subtitle">Caregiver Monitor</p>
          </div>
        </div>
        <ConnectionPanel />
      </div>
    </header>

    <!-- Main Content -->
    <main class="app-main">
      <!-- Vitals Cards -->
      <section class="vitals-row">
        <VitalsCard
          label="Heart Rate"
          :value="heartRate"
          unit="BPM"
          color="red"
          :decimals="0"
        />
        <VitalsCard
          label="EMG Tension"
          :value="emgEnvelope"
          unit="μV"
          color="blue"
          :decimals="1"
        />
        <VitalsCard
          label="Motion"
          :value="motionMagnitude"
          unit="g"
          color="green"
          :decimals="2"
        />
      </section>

      <!-- Chart + Alert Log -->
      <section class="dashboard-grid">
        <LiveChart />
        <AlertLog />
      </section>
    </main>

    <!-- Footer -->
    <footer class="app-footer">
      <p>Senseware Wearable System &middot; Web Bluetooth requires <strong>Chrome</strong> or <strong>Edge</strong> on <strong>HTTPS/localhost</strong></p>
    </footer>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import ConnectionPanel from './components/ConnectionPanel.vue'
import VitalsCard from './components/VitalsCard.vue'
import LiveChart from './components/LiveChart.vue'
import AlertLog from './components/AlertLog.vue'
import { useBluetooth } from './composables/useBluetooth.js'

const ble = useBluetooth()

const heartRate = ref(0)
const emgEnvelope = ref(0)
const motionMagnitude = ref(0)

const unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ble.on('telemetry', ({ heartRate: hr, emgEnvelope: emg, motionMagnitude: motion }) => {
      heartRate.value = hr
      emgEnvelope.value = emg
      motionMagnitude.value = motion
    })
  )

  // Reset values on disconnect
  unsubscribers.push(
    ble.on('disconnected', () => {
      heartRate.value = 0
      emgEnvelope.value = 0
      motionMagnitude.value = 0
    })
  )
})

onUnmounted(() => {
  unsubscribers.forEach((u) => u())
})
</script>

<style scoped>
.app-shell {
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}

/* Header */
.app-header {
  background: var(--bg-secondary);
  border-bottom: 1px solid var(--border);
  padding: 1rem 1.5rem;
  position: sticky;
  top: 0;
  z-index: 100;
  backdrop-filter: blur(12px);
}

.header-content {
  max-width: 1200px;
  margin: 0 auto;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1.5rem;
  flex-wrap: wrap;
}

.header-brand {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}

.brand-icon {
  font-size: 1.5rem;
}

.brand-title {
  font-size: 1.25rem;
  font-weight: 700;
  color: var(--text-primary);
  line-height: 1.2;
  margin: 0;
}

.brand-subtitle {
  font-size: 0.78rem;
  color: var(--text-muted);
  font-weight: 500;
  margin: 0;
  text-transform: uppercase;
  letter-spacing: 1px;
}

/* Main Content */
.app-main {
  flex: 1;
  max-width: 1200px;
  width: 100%;
  margin: 0 auto;
  padding: 1.5rem;
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

/* Vitals Row */
.vitals-row {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 1rem;
}

/* Dashboard Grid: Chart + Alert Log side by side on desktop */
.dashboard-grid {
  display: grid;
  grid-template-columns: 1.5fr 1fr;
  gap: 1.5rem;
  align-items: start;
}

/* Footer */
.app-footer {
  padding: 1rem 1.5rem;
  border-top: 1px solid var(--border);
  text-align: center;
}

.app-footer p {
  color: var(--text-muted);
  font-size: 0.75rem;
}

.app-footer strong {
  color: var(--text-secondary);
}

/* Responsive */
@media (max-width: 900px) {
  .dashboard-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 640px) {
  .vitals-row {
    grid-template-columns: 1fr;
  }

  .header-content {
    flex-direction: column;
    align-items: flex-start;
    gap: 1rem;
  }

  .app-header {
    padding: 0.75rem 1rem;
  }

  .app-main {
    padding: 1rem;
    gap: 1rem;
  }
}
</style>
