<script setup>
import FarmManagementPanel from "../components/FarmManagementPanel.vue";
</script>

<template>
  <div class="archives-page">
    <FarmManagementPanel />
  </div>
</template>

<style scoped>
.archives-page {
  height: 100%;
  display: flex;
  flex-direction: column;
  background:
    url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.7' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)' opacity='0.025'/%3E%3C/svg%3E"),
    linear-gradient(180deg,
      #f5efe0 0%,
      #e8f0e0 25%,
      #d4e8c8 45%,
      #c5d8b0 60%,
      #b8c8a0 78%,
      #9aaf82 92%,
      #8a9e78 100%
    );
  border-radius: 16px;
  border: 1px solid rgba(59,107,59,0.06);
  overflow-y: auto;
}
</style>
