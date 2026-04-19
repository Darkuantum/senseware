<template>
  <div class="app-shell">
    <!-- Header -->
    <header class="app-header">
      <div class="header-content">
        <div class="header-left">
          <router-link to="/" class="brand-link">
            <Activity :size="20" class="brand-icon" />
            <div>
              <h1 class="brand-title">Senseware</h1>
              <p class="brand-subtitle">Caregiver Monitor</p>
            </div>
          </router-link>
        </div>
        <div class="header-right">
          <router-link to="/" class="nav-home">
            <Home :size="16" />
            <span>Home</span>
          </router-link>
          <ConnectionPanel />
        </div>
      </div>
    </header>

    <!-- Main Content -->
    <main class="app-main">
      <!-- Vitals Cards -->
      <section class="vitals-row">
        <VitalsCard
          key="hr"
          label="Heart Rate"
          :value="heartRate"
          unit="BPM"
          color="danger"
          icon-name="Heart"
          :decimals="0"
        />
        <VitalsCard
          key="spo2"
          label="SpO2"
          :value="spo2"
          unit="%"
          color="purple"
          icon-name="Droplets"
          :decimals="1"
        />
        <VitalsCard
          key="emg"
          label="EMG Tension"
          :value="emgEnvelope"
          unit="μV"
          color="info"
          icon-name="Zap"
          :decimals="1"
        />
        <VitalsCard
          key="mot"
          label="Motion"
          :value="motionMagnitude"
          unit="g"
          color="success"
          icon-name="Activity"
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
      <p>Senseware Wearable System &middot; Connects via <strong>HTTP</strong> to ESP32 on port 81</p>
    </footer>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { Activity, Home } from 'lucide-vue-next'
import ConnectionPanel from '../components/ConnectionPanel.vue'
import VitalsCard from '../components/VitalsCard.vue'
import LiveChart from '../components/LiveChart.vue'
import AlertLog from '../components/AlertLog.vue'
import { useTelemetry } from '../composables/useTelemetry.js'

const ws = useTelemetry()

const heartRate = ref(0)
const spo2 = ref(0)
const emgEnvelope = ref(0)
const motionMagnitude = ref(0)

const unsubscribers = []

onMounted(() => {
  unsubscribers.push(
    ws.manager.on('telemetry', (data) => {
      heartRate.value = (typeof data.hr === 'number' && isFinite(data.hr)) ? data.hr : 0
      spo2.value = (typeof data.spo2 === 'number' && isFinite(data.spo2)) ? data.spo2 : 0
      emgEnvelope.value = (typeof data.emg === 'number' && isFinite(data.emg)) ? data.emg : 0
      motionMagnitude.value = (typeof data.mot === 'number' && isFinite(data.mot)) ? data.mot : 0
    })
  )

  // Reset values on disconnect
  unsubscribers.push(
    ws.manager.on('stateChange', (newState) => {
      if (newState === 'disconnected') {
        heartRate.value = 0
        spo2.value = 0
        emgEnvelope.value = 0
        motionMagnitude.value = 0
      }
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
  padding: 0.75rem 1.5rem;
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
}

.header-left {
  display: flex;
  align-items: center;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 1rem;
}

.brand-link {
  display: flex;
  align-items: center;
  gap: 0.65rem;
  text-decoration: none;
  color: inherit;
}

.brand-icon {
  color: var(--accent);
}

.brand-title {
  font-size: 1.15rem;
  font-weight: 700;
  color: var(--text-primary);
  line-height: 1.2;
  margin: 0;
}

.brand-subtitle {
  font-size: 0.72rem;
  color: var(--text-muted);
  font-weight: 500;
  margin: 0;
  text-transform: uppercase;
  letter-spacing: 1px;
}

.nav-home {
  display: flex;
  align-items: center;
  gap: 0.35rem;
  font-size: 0.82rem;
  font-weight: 500;
  color: var(--text-secondary);
  text-decoration: none;
  padding: 0.4rem 0.75rem;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  transition: all var(--transition-fast);
}

.nav-home:hover {
  color: var(--text-primary);
  border-color: var(--border-hover);
  background: var(--card-bg);
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

/* Vitals Row — 4 cards */
.vitals-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
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
    grid-template-columns: repeat(2, 1fr);
  }

  .header-content {
    flex-direction: column;
    align-items: flex-start;
    gap: 0.75rem;
  }

  .header-right {
    width: 100%;
    justify-content: flex-end;
  }

  .nav-home span {
    display: none;
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
