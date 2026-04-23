<template>
  <div class="app-shell" :class="{ 'mobile-frame': mobileMode }">
    <header class="app-header">
      <div class="header-content">
        <div class="header-left">
          <router-link to="/" class="brand-link">
            <img src="/logo.png" alt="Senseware" class="brand-logo" />
            <div>
              <h1 class="brand-title">Senseware</h1>
              <p class="brand-subtitle">Caregiver Monitor</p>
            </div>
          </router-link>
        </div>
        <div class="header-right">
          <router-link to="/" class="nav-home" title="Home">
            <Home :size="16" />
          </router-link>
          <ConnectionPanel />
          <button class="btn-viewport" :class="{ active: mobileMode }" @click="mobileMode = !mobileMode" title="Toggle mobile preview">
            <Smartphone :size="16" />
          </button>
        </div>
      </div>
    </header>

    <main class="app-main">
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

      <section class="stats-row">
        <SessionStats />
      </section>

      <section class="gauge-row">
        <AnomalyGauge />
      </section>

      <section class="dashboard-grid">
        <LiveChart />
        <AlertLog />
      </section>
    </main>

    <footer class="app-footer">
      <p>Senseware Wearable System &middot; Connects via <strong>HTTP</strong> to ESP32 on port 81</p>
    </footer>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { Home, Smartphone } from 'lucide-vue-next'
import ConnectionPanel from '../components/ConnectionPanel.vue'
import VitalsCard from '../components/VitalsCard.vue'
import LiveChart from '../components/LiveChart.vue'
import AlertLog from '../components/AlertLog.vue'
import SessionStats from '../components/SessionStats.vue'
import AnomalyGauge from '../components/AnomalyGauge.vue'
import { useTelemetry } from '../composables/useTelemetry.js'

const ws = useTelemetry()
const mobileMode = ref(false)

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
  height: 100%;
 overflow-y: auto;
  container-type: inline-size;
  container-name: dashboard;
}

.app-shell.mobile-frame .app-main {
  overflow-y: auto;
  flex: 1;
  min-height: 0;
}

.app-shell.mobile-frame {
  max-width: 393px;
  aspect-ratio: 9 / 19.5;
  max-height: calc(100vh - 2rem);
  margin: 1rem auto;
  border-radius: 40px;
  border: 3px solid rgba(255, 255, 255, 0.12);
  box-shadow:
    0 0 0 8px #1a1a2e,
    0 0 0 10px rgba(255, 255, 255, 0.06),
    0 0 60px rgba(139, 92, 246, 0.12);
  overflow: hidden;
  height: min(calc(100vh - 2rem), calc(393px * 19.5 / 9));
  background: var(--bg-primary);
}

.app-header {
  background: rgba(11, 9, 20, 0.85);
  border-bottom: 1px solid var(--border);
  padding: 0.75rem 1.5rem;
  position: sticky;
  top: 0;
  z-index: 100;
  backdrop-filter: blur(12px);
}

.mobile-frame .app-header {
  border-radius: 37px 37px 0 0;
  padding: 0.6rem 1.25rem;
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

.brand-logo {
  height: 28px;
  width: auto;
  object-fit: contain;
  margin-right: 0.5rem;
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
  justify-content: center;
  width: 34px;
  height: 34px;
  font-size: 0.82rem;
  font-weight: 500;
  color: var(--text-secondary);
  text-decoration: none;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: rgba(255, 255, 255, 0.04);
  transition: all var(--transition-fast);
}

.nav-home:hover {
  color: var(--text-primary);
  border-color: var(--border-hover);
  background: var(--card-bg-hover);
}

.btn-viewport {
  display: flex;
  align-items: center;
  justify-content: center;
  width: 34px;
  height: 34px;
  border-radius: var(--radius-sm);
  border: 1px solid var(--border);
  background: transparent;
  color: var(--text-muted);
  cursor: pointer;
  transition: all var(--transition-fast);
}

.btn-viewport:hover {
  color: var(--text-primary);
  border-color: var(--border-hover);
  background: rgba(255, 255, 255, 0.06);
}

.btn-viewport.active {
  color: var(--accent);
  border-color: var(--accent);
  background: rgba(139, 92, 246, 0.12);
}

.app-main {
  flex: 1;
  max-width: 1200px;
  width: 100%;
  margin: 0 auto;
  padding: 1.5rem;
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
  min-width: 0;
}

.vitals-row {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 1rem;
}

.vitals-row > * {
  min-width: 0;
}

.stats-row {
  display: grid;
  grid-template-columns: 1fr;
}

.gauge-row {
  display: grid;
  grid-template-columns: 1fr;
}

.dashboard-grid {
  display: grid;
  grid-template-columns: 1.5fr 1fr;
  gap: 1.5rem;
  align-items: start;
}

.dashboard-grid > * {
  min-width: 0;
  overflow: hidden;
}

.app-footer {
  padding: 1rem 1.5rem;
  border-top: 1px solid var(--border);
  text-align: center;
}

.mobile-frame .app-footer {
  border-radius: 0 0 37px 37px;
  padding: 0.75rem 1rem;
}

.app-footer p {
  color: var(--text-muted);
  font-size: 0.75rem;
}

.app-footer strong {
  color: var(--text-secondary);
}

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

@container dashboard (max-width: 500px) {
  .vitals-row {
    grid-template-columns: repeat(2, 1fr);
    gap: 0.5rem;
  }

  .app-main {
    padding: 0.6rem;
    gap: 0.6rem;
  }

  .dashboard-grid {
    grid-template-columns: 1fr;
    gap: 0.6rem;
  }

  .header-content {
    flex-wrap: wrap;
    gap: 0.4rem;
  }

  .header-right {
    gap: 0.4rem;
  }

  .brand-subtitle {
    display: none;
  }

  .nav-home {
    display: flex;
  }

  .btn-viewport {
    display: none;
  }

  .brand-logo {
    height: 22px;
    margin-right: 0.3rem;
  }

  .brand-title {
    font-size: 0.95rem;
  }
}
</style>
