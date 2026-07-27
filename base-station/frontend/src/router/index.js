import { createRouter, createWebHistory } from "vue-router";

const router = createRouter({
  history: createWebHistory(),
  routes: [
    {
      path: "/",
      name: "dashboard",
      component: () => import("../views/DashboardView.vue"),
    },
    {
      path: "/monitor",
      name: "monitor",
      component: () => import("../views/LiveMonitorView.vue"),
    },
    {
      path: "/archives",
      name: "archives",
      component: () => import("../views/ArchivesView.vue"),
    },
    {
      path: "/knowledge",
      name: "knowledge",
      component: () => import("../views/KnowledgeBaseView.vue"),
    },
    {
      path: "/plot/:code",
      name: "plot-detail",
      component: () => import("../views/PlotDetailView.vue"),
      props: true,
    },
  ],
});

export default router;
