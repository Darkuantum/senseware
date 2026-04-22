<template>
  <div ref="container" class="mermaid-container" v-html="svgHtml"></div>
</template>

<script>
// Module-scoped: runs once when the component file is imported
import mermaid from 'mermaid'

let idCounter = 0

mermaid.initialize({
  startOnLoad: false,
  theme: 'base',
  themeVariables: {
    primaryColor: '#1A1A2E',
    primaryTextColor: '#FFFFFF',
    primaryBorderColor: '#8B5CF6',
    lineColor: '#8B5CF6',
    secondaryColor: '#252540',
    tertiaryColor: '#0B0914',
    fontFamily: 'Nunito, sans-serif',
    fontSize: '14px',
    background: '#0B0914',
    mainBkg: '#1A1A2E',
    nodeBorder: '#8B5CF6',
    clusterBkg: '#1A1625',
    titleColor: '#FFFFFF',
    edgeLabelBackground: '#0B0914',
  },
})

export { idCounter, mermaid }
</script>

<script setup>
import { ref, onMounted, watch } from 'vue'

const props = defineProps({
  chart: { type: String, required: true },
})

const svgHtml = ref('')
const container = ref(null)

// Use the module-scoped idCounter from the regular <script> block
async function render() {
  try {
    const id = `mermaid-${++idCounter}`
    const { svg } = await mermaid.render(id, props.chart)
    svgHtml.value = svg
  } catch (e) {
    console.warn('Mermaid render error:', e)
    svgHtml.value = `<pre class="mermaid-error">${props.chart}</pre>`
  }
}

onMounted(() => render())
watch(() => props.chart, () => render())
</script>

<style scoped>
.mermaid-container {
  display: flex;
  justify-content: center;
  padding: 1rem 0;
  overflow-x: auto;
}

.mermaid-container :deep(svg) {
  max-width: 100%;
  height: auto;
}

.mermaid-error {
  color: var(--color-danger);
  font-size: 0.85rem;
  background: rgba(239, 68, 68, 0.06);
  padding: 1rem;
  border-radius: var(--radius-sm);
  white-space: pre-wrap;
}
</style>
