import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  { path: '/', name: 'Landing', component: () => import('./views/LandingPage.vue') },
  { path: '/dashboard', name: 'Dashboard', component: () => import('./views/DashboardPage.vue') },
]

export default createRouter({
  history: createWebHistory(),
  routes,
})
