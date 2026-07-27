<template>
  <div class="farm-panel" :class="{ 'detail-mode': !!selectedBlockId }">
    <section v-if="!selectedBlockId" class="farm-status-grid">
      <article class="farm-card status-card total">
        <p class="status-title">农田总数</p>
        <p class="status-value">{{ summary.total }}</p>
      </article>
      <article class="farm-card status-card normal">
        <p class="status-title">正常</p>
        <p class="status-value">{{ summary.normal }}</p>
      </article>
      <article class="farm-card status-card warning">
        <p class="status-title">预警</p>
        <p class="status-value">{{ summary.warning }}</p>
      </article>
      <article class="farm-card status-card danger">
        <p class="status-title">危险</p>
        <p class="status-value">{{ summary.danger }}</p>
      </article>
    </section>

    <!-- 待人工复核横幅 -->
    <section v-if="!selectedBlockId && reviewItems.length" class="review-banner" :class="{ collapsed: !showReviewBanner }">
      <div class="review-banner-head" @click="showReviewBanner = !showReviewBanner">
        <span class="review-banner-icon">⚠️</span>
        <span class="review-banner-title">待人工复核 ({{ reviewItems.length }})</span>
        <span class="review-banner-arrow">{{ showReviewBanner ? '▾' : '▸' }}</span>
      </div>
      <div v-if="showReviewBanner" class="review-banner-body">
        <div
          v-for="(item, idx) in reviewItems" :key="idx"
          :class="['review-chip', item.type]"
          @click="openDetail(item.block_id)"
        >
          <span class="review-chip-id">{{ item.block_id }}</span>
          <span class="review-chip-reason">{{ item.reason }}</span>
          <span class="review-chip-action">审查 →</span>
        </div>
      </div>
    </section>

    <section v-if="!selectedBlockId" class="farm-main-grid">
      <article class="farm-card table-card">
        <div class="table-top">
          <div>
            <h3>农田管理</h3>
            <p>接收来自 ESP32/基站的区块状态，支持删除和查看详情。</p>
          </div>
          <div class="table-tools">
            <input
              v-model="keyword"
              type="text"
              class="search-input"
              placeholder="搜索区块ID/作物/状态/严重度"
            />
            <button type="button" class="primary-btn" :disabled="loading" @click="loadFarmData">
              {{ loading ? "刷新中..." : "刷新" }}
            </button>
          </div>
        </div>

        <div class="table-wrap">
          <div class="thead">
            <span>序号</span>
            <span>区块ID</span>
            <span>农作物种类</span>
            <span>预计采收</span>
            <span class="col-status">治理状态</span>
            <span class="col-time">更新时间</span>
            <span>操作</span>
          </div>

          <div v-if="loading && !plots.length" class="table-state">加载中...</div>
          <div v-else-if="!filteredPlots.length" class="table-state">
            {{ keyword.trim() ? "未匹配到区块" : "暂无区块数据" }}
          </div>

          <div
            v-for="item in filteredPlots"
            :key="item.block_id"
            class="tbody-row"
            :class="{ selected: selectedBlockId === item.block_id, danger: statusText(item) === '严重' }"
          >
            <span>{{ item.seq }}</span>
            <span>{{ item.block_id }}</span>
            <span>{{ item.crop_type || "未知" }}</span>
            <span v-if="editingBlockId === item.block_id">
              <input v-model="editHarvestDate" type="date" class="edit-input"
                @keyup.enter="saveHarvestDate(item.block_id)"
                @blur="saveHarvestDate(item.block_id)" />
            </span>
            <span v-else>{{ item.expected_harvest_at || "-" }}</span>
            <span class="col-status" :class="statusClass(item)">
              {{ statusText(item) }}
            </span>
            <span class="col-time">{{ formatDate(item.updated_at) }}</span>
            <span class="actions">
              <template v-if="editingBlockId === item.block_id">
                <button type="button" class="danger-btn" @click="removePlot(item.block_id)">删除</button>
                <button type="button" class="secondary-btn" @click="cancelEdit()">取消</button>
              </template>
              <template v-else>
                <button type="button" class="secondary-btn" @click="openDetail(item.block_id)">详细</button>
                <button type="button" class="edit-btn" @click="startEdit(item)">编辑</button>
              </template>
            </span>
          </div>
        </div>
      </article>
    </section>

    <section v-else class="detail-only-wrap">
      <article class="farm-card detail-card detail-only-card">
        <div class="detail-top">
          <h3>区块（{{ detail.plot?.code || selectedBlockId }}）详细信息</h3>
          <div class="detail-top-actions">
            <button type="button" :class="['edit-mode-btn', { active: editMode }]" @click="editMode = !editMode">
              {{ editMode ? '✓ 完成' : '✎ 编辑' }}
            </button>
            <button type="button" class="close-btn" @click="closeDetail">关闭</button>
          </div>
        </div>
        <p v-if="detail.plot" class="detail-sub-title">{{ detail.plot.name }}</p>

        <div v-if="detailLoading" class="detail-empty">详情加载中...</div>
        <div v-else-if="!detail.plot" class="detail-empty">正在加载区块信息...</div>
        <div v-else class="detail-content">
          <section class="latest-grid">
            <div class="latest-item">
              <label>当前状态</label>
              <strong>{{ detail.latest?.status_label || "-" }}</strong>
            </div>
            <div class="latest-item">
              <label>严重度</label>
              <strong :class="{ red: latestSeverity === '严重' }">
                {{ latestSeverity }}
              </strong>
            </div>
            <div class="latest-item">
              <label>最近病虫害</label>
              <strong>{{ (detail.latest?.pest_type && detail.latest.pest_type !== 'none') ? detail.latest.pest_type : '暂无病虫害' }}</strong>
            </div>
            <div class="latest-item">
              <label>施药类型</label>
              <strong>{{ (detail.latest?.pest_type && detail.latest.pest_type !== 'none') ? (detail.latest?.pesticide || detail.latest?.action_type || '-') : '无需施药' }}</strong>
            </div>
            <div class="latest-item">
              <label>天气 / 温湿度</label>
              <strong>
                <template v-if="weather.today">
                  {{ weather.today.weather }} · {{ weather.today.low }}~{{ weather.today.high }}℃
                </template>
                <template v-else>
                  {{ formatNumber(detail.latest?.temperature) }} / {{ formatNumber(detail.latest?.humidity) }}
                </template>
              </strong>
            </div>
            <div class="latest-item">
              <label>最近更新</label>
              <strong>{{ (detail.latest?.updated_at || '').slice(0, 10) || '-' }}</strong>
            </div>
          </section>

          <section class="block-section ai-presc-section" v-if="detail.prescription">
            <h4 class="ai-title">
              <span class="ai-title-icon">💊</span>
              <span>电子处方与防抗药性决策</span>
              <button v-if="prescExcluded.length" class="ai-toggle" @click="prescExpanded = !prescExpanded">
                {{ prescExpanded ? '收起' : '展开' }}
              </button>
            </h4>

            <div v-if="detail.prescription.warnings?.length" class="warnings">
              <div v-for="w in detail.prescription.warnings" :key="w" class="warning-box">
                ⚠️ {{ w }}
              </div>
            </div>

            <div class="timeline" v-if="detail.prescription.timeline?.length">
              <div v-for="(t, idx) in detail.prescription.timeline" :key="idx" :class="['timeline-item', t.status]">
                <div class="dot"></div>
                <div class="content">
                  <span class="date">{{ t.date }}</span>
                  <span class="event">{{ t.event }}</span>
                </div>
              </div>
            </div>

            <!-- 推荐方案 -->
            <div v-if="prescRecommend.length || !prescExcluded.length" class="rec-hero">
              <div class="rec-hero-bar">
                <span class="rec-hero-label">✅ 推荐方案</span>
                <button class="rec-hero-toggle" @click="recHeroExpanded = !recHeroExpanded">
                  {{ recHeroExpanded ? '▾ 收起' : '▸ 展开' }}
                </button>
              </div>
              <div v-if="recHeroExpanded">
                <div
                  v-for="r in prescRecommend" :key="r.pesticide_id"
                  :class="['rec-card', 'recommended', { warn: !r.is_recommended }]"
                >
                  <div class="rec-header">
                    <h5>{{ r.name }} ({{ r.type }})</h5>
                    <span class="tag" v-for="tag in r.tags" :key="tag">{{ tag }}</span>
                  </div>
                  <div class="rec-body">
                    <p>剂量基准: {{ r.base_dosage }} | PHI安全期: {{ r.phi_days }}天</p>
                    <p class="reason">{{ r.reason }}</p>
                  </div>
                </div>
              </div>
            </div>
            <div v-else class="rec-hero">
              <div class="rec-hero-label">⚠️ 无可推荐方案</div>
              <p class="rec-hero-hint">所有药剂均被PHI安全锁拦截，建议物理/生物防治</p>
            </div>

            <!-- 已排除方案 — 可折叠 -->
            <div v-if="prescExcluded.length" class="rec-excluded" :class="{ open: prescExpanded }">
              <button class="rec-excluded-toggle" @click="prescExpanded = !prescExpanded">
                <span>已排除 {{ prescExcluded.length }} 种</span>
                <span class="rec-excluded-arrow">{{ prescExpanded ? '▾' : '▸' }}</span>
              </button>
              <div v-if="prescExpanded" class="rec-excluded-body">
                <div v-for="r in prescExcluded" :key="r.pesticide_id" class="rec-card locked">
                  <div class="rec-header">
                    <h5>{{ r.name }} ({{ r.type }})</h5>
                    <span class="tag">PHI超标禁止</span>
                  </div>
                  <div class="rec-body">
                    <p>剂量基准: {{ r.base_dosage }} | PHI安全期: {{ r.phi_days }}天</p>
                    <p class="reason">{{ r.reason }}</p>
                  </div>
                </div>
              </div>
            </div>
          </section>

          <section v-if="fruitTimelineEntries.length" class="block-section">
            <h4 class="section-toggle-h4">
              <span>🍎 果实情况</span>
              <button class="toggle-btn" @click="fruitExpanded = !fruitExpanded">
                {{ fruitExpanded ? '▾ 收起' : '▸ 展开' }}
              </button>
            </h4>
            <template v-if="fruitExpanded">
            <div class="emr-timeline">
              <div v-for="grp in fruitTimelineEntries" :key="'fruit-' + grp.dateKey" class="emr-day">
                <div class="emr-date-head">
                  <span class="emr-date-dot">●</span>
                  <span class="emr-date-text">{{ grp.dateKey }}</span>
                  <span class="emr-date-line"></span>
                </div>
                <div class="emr-day-body">
                  <div v-for="blk in grp.fruitBlocks" :key="'fruit-' + grp.dateKey + '-' + blk.pest_type" class="emr-pest-card" style="border-color:#f0c78e">
                    <div class="emr-pest-card-hd">
                      <span class="emr-chip-icon">🍎</span>
                      <span class="emr-chip-tag">果实</span>
                      <span class="emr-card-title">{{ blk.pest_type }}</span>
                      <span class="emr-card-sev" style="color:#c7852a">📸 {{ blk.total }}张</span>
                    </div>
                    <div v-if="blk.images.length" class="emr-image-row">
                      <div
                        v-for="(img, idx) in blk.images" :key="img.event_id"
                        class="emr-thumb"
                        :class="{ 'editing': editMode }"
                        @click="!editMode && openLightbox(blk.images.map(x=>resolveImageUrl(x.image_url)), idx)"
                      >
                        <img :src="resolveImageUrl(img.image_url)" alt="果实实拍图" />
                        <template v-if="editMode">
                          <select
                            class="emr-type-edit"
                            :value="img.pest_type || ''"
                            :disabled="savingCategoryEventId === img.event_id || deletingEventId === img.event_id"
                            @change="updateImageCategory(img.event_id, $event.target.value)"
                            @click.stop
                          >
                            <option value="" disabled>选择类别</option>
                            <option v-for="option in imageCategoryOptions" :key="'fruit-' + option" :value="option">
                              {{ option }}
                            </option>
                          </select>
                          <input
                            type="date"
                            class="emr-date-edit"
                            :value="(img.created_at||'').slice(0,10)"
                            :disabled="savingCategoryEventId === img.event_id || deletingEventId === img.event_id"
                            @change="moveImageDate(img.event_id, $event.target.value)"
                            @click.stop
                          />
                          <button
                            class="emr-del-btn"
                            :disabled="savingCategoryEventId === img.event_id || deletingEventId === img.event_id"
                            @click.stop="deleteImage(img.event_id)"
                            title="删除"
                          >×</button>
                        </template>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            </template>
          </section>

          <section class="block-section">
            <h4 class="section-toggle-h4">
              <span>📋 电子药历</span>
              <button class="toggle-btn" @click="emrExpanded = !emrExpanded">
                {{ emrExpanded ? '▾ 收起' : '▸ 展开' }}
              </button>
            </h4>
            <template v-if="emrExpanded">
            <p v-if="!timelineEntries.length" class="muted">暂无记录</p>
            <div v-else class="emr-timeline">
              <div v-for="grp in timelineEntries" :key="grp.dateKey" class="emr-day">
                <div class="emr-date-head">
                  <span class="emr-date-dot">●</span>
                  <span class="emr-date-text">{{ grp.dateKey }}</span>
                  <span class="emr-date-line"></span>
                </div>
                <div class="emr-day-body">
                  <!-- 每种病害一个独立卡片 -->
                  <div v-for="blk in grp.pestBlocks" :key="grp.dateKey + '-' + blk.pest_type" class="emr-pest-card">
                    <div class="emr-pest-card-hd">
                      <span class="emr-chip-icon">{{ isInsect(blk.pest_type) ? '🐛' : '🍂' }}</span>
                      <span class="emr-chip-tag">{{ isInsect(blk.pest_type) ? '虫害' : '叶病' }}</span>
                      <span class="emr-card-title">{{ blk.pest_type }}</span>
                      <span :class="['emr-card-sev', blk.sev==='严重'?'sev-red':blk.sev==='轻度'?'sev-amber':'sev-green']">{{ blk.sev==='严重'?'🔴':blk.sev==='轻度'?'🟠':'🟢' }} {{ blk.sev }}</span>
                    </div>
                    <div v-if="blk.images.length" class="emr-image-row">
                      <div
                        v-for="(img, idx) in blk.images" :key="img.event_id"
                        class="emr-thumb"
                        :class="{ 'editing': editMode }"
                        @click="!editMode && openLightbox(blk.images.map(x=>resolveImageUrl(x.image_url)), idx)"
                      >
                        <img :src="resolveImageUrl(img.image_url)" alt="实拍图" />
                        <template v-if="editMode">
                          <select
                            class="emr-type-edit"
                            :value="img.pest_type || ''"
                            :disabled="savingCategoryEventId === img.event_id || deletingEventId === img.event_id"
                            @change="updateImageCategory(img.event_id, $event.target.value)"
                            @click.stop
                          >
                            <option value="" disabled>选择类别</option>
                            <option v-for="option in imageCategoryOptions" :key="'pest-' + option" :value="option">
                              {{ option }}
                            </option>
                          </select>
                          <input
                            type="date"
                            class="emr-date-edit"
                            :value="(img.created_at||'').slice(0,10)"
                            :disabled="savingCategoryEventId === img.event_id || deletingEventId === img.event_id"
                            @change="moveImageDate(img.event_id, $event.target.value)"
                            @click.stop
                          />
                          <button
                            class="emr-del-btn"
                            :disabled="savingCategoryEventId === img.event_id || deletingEventId === img.event_id"
                            @click.stop="deleteImage(img.event_id)"
                            title="删除"
                          >×</button>
                        </template>
                      </div>
                    </div>
                  </div>
                  <!-- 无图片的作业日志 -->
                  <div v-if="grp.ops.length" class="emr-chips" style="margin-top:6px">
                    <div v-for="(op, oi) in grp.ops" :key="oi" class="emr-chip op">
                      <span class="emr-chip-icon">🔧</span>
                      <span>{{ op.action_type }} · {{ op.result }} · {{ op.operator }}</span>
                    </div>
                  </div>
                </div>
              </div>
            </div>
            </template>
          </section>
        </div>
      </article>

      <!-- 图片灯箱弹窗（支持左右切换） -->
      <div v-if="lightbox.images.length" class="lightbox-overlay" @click.self="closeLightbox" @keydown="onLightboxKey">
        <button class="lightbox-close" @click="closeLightbox">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/></svg>
        </button>
        <button v-if="lightbox.images.length > 1" class="lightbox-prev" @click.stop="prevLightbox">
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="15 18 9 12 15 6"/></svg>
        </button>
        <img :src="lightbox.images[lightbox.idx]" class="lightbox-img" />
        <div v-if="lightbox.images.length > 1" class="lightbox-counter">{{ lightbox.idx + 1 }} / {{ lightbox.images.length }}</div>
        <button v-if="lightbox.images.length > 1" class="lightbox-next" @click.stop="nextLightbox">
          <svg width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="9 18 15 12 9 6"/></svg>
        </button>
      </div>
    </section>

    <p v-if="errorText" class="error-line">{{ errorText }}</p>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, reactive, ref } from "vue";
import {
  apiBase,
  clearFarmPlot,
  deleteDeviceEvent,
  getFarmPlotDetail,
  getFarmPlotPrescription,
  getFarmPlots,
  getFarmStatus,
  getFarmWeather,
  handleResponse,
  patchDeviceEvent,
} from "../api/client.js";

const loading = ref(false);
const detailLoading = ref(false);
const errorText = ref("");
const editMode = ref(false);
const savingCategoryEventId = ref("");
const deletingEventId = ref("");
const selectedBlockId = ref("");
const keyword = ref("");
const lightbox = reactive({ images: [], idx: 0 });
function openLightbox(images, startIdx = 0) {
  lightbox.images = images;
  lightbox.idx = startIdx;
  document.addEventListener('keydown', onLightboxKey);
}
function closeLightbox() {
  lightbox.images = [];
  lightbox.idx = 0;
  document.removeEventListener('keydown', onLightboxKey);
}
function prevLightbox() { if (lightbox.idx > 0) lightbox.idx--; }
function nextLightbox() { if (lightbox.idx < lightbox.images.length - 1) lightbox.idx++; }
function onLightboxKey(e) {
  if (e.key === 'ArrowLeft') prevLightbox();
  else if (e.key === 'ArrowRight') nextLightbox();
  else if (e.key === 'Escape') closeLightbox();
}

// 电子处方折叠 + 推荐/排除分离
const prescExpanded = ref(false);
const recHeroExpanded = ref(false);
const fruitExpanded = ref(true);
const emrExpanded = ref(true);
const prescRecommend = computed(() => (detail.prescription?.recommendations || []).filter(r => !r.is_locked));
const prescExcluded = computed(() => (detail.prescription?.recommendations || []).filter(r => r.is_locked));

const summary = reactive({
  total: 0,
  normal: 0,
  warning: 0,
  danger: 0,
  updated_at: "",
});

const plots = ref([]);
const editingBlockId = ref("");
const editHarvestDate = ref("");

const weather = reactive({
  location: "",
  today: null, // { weather, high, low, wind, rain }
});

async function loadWeather() {
  try {
    const data = await getFarmWeather();
    weather.location = data.location || "";
    if (data.forecast?.length) {
      weather.today = data.forecast[0];
    }
  } catch {
    // 静默失败
  }
}

function formatDate(isoStr) {
  if (!isoStr) return "-";
  return isoStr.slice(0, 10);
}

const isReviewPhase = computed(() => {
  const a06 = plots.value.find((item) => item.block_id === "A-06");
  return !!a06 && ["running", "completed"].includes(a06.status);
});

// 与监控页状态机同步：scheduled → 初始态, completed → 完成态, running → 动态
const monitorPhase = ref((sessionStorage.getItem('agri_sim_status') || 'scheduled'));

const FARM_INITIAL_STATUS = {
  "A-01": "轻度",
  "A-02": "正常",
  "A-03": "正常",
  "A-04": "严重",
  "A-05": "轻度",
  "A-06": "正常",
};
const FARM_COMPLETED_STATUS = {
  "A-01": "严重",
  "A-02": "轻度",
  "A-03": "轻度",
  "A-04": "轻度",
  "A-05": "正常",
  "A-06": "待复查",
};

// 列表页治理状态：与监控页固定映射一一对应
// 01严重 02/03/04轻度 05正常 06待复查（有图时），初始01轻度异常 04病害预警 05轻度异常 其余正常
function statusText(item) {
  // 监控处于初始待机态 → 固定初始状态，不等后端 governance
  if (monitorPhase.value === 'scheduled') {
    return FARM_INITIAL_STATUS[item.block_id] || '正常';
  }
  // 监控处于完成展示态 → 固定完成态
  if (monitorPhase.value === 'completed') {
    return FARM_COMPLETED_STATUS[item.block_id] || '正常';
  }
  // running 态 → 使用后端动态 governance
  if (item.block_id === 'A-06') return '待复查';
  const g = item.governance || '';
  if (g === '严重' || g === '病害预警') return '严重';
  if (g === '轻度' || g === '轻度异常') return '轻度';
  return '正常';
}
function statusClass(item) {
  const text = statusText(item);
  if (text === '待复查') return 'status-review';
  if (text === '严重') return 'status-pending';
  if (text === '轻度') return 'status-mild';
  return 'status-done';
}

function startEdit(item) {
  editingBlockId.value = item.block_id;
  editHarvestDate.value = item.expected_harvest_at || "";
}
async function cancelEdit() {
  editingBlockId.value = "";
  editHarvestDate.value = "";
}
async function saveHarvestDate(blockId) {
  if (!editHarvestDate.value) return;
  try {
    await fetch(`${apiBase}/api/farm/plots/${encodeURIComponent(blockId)}/harvest`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ expected_harvest_at: editHarvestDate.value }),
    }).then(handleResponse);
    editingBlockId.value = "";
    editHarvestDate.value = "";
    await loadFarmData();
  } catch (e) {
    errorText.value = "保存失败: " + e.message;
  }
}

const detail = reactive({
  plot: null,
  latest: null,
  images: [],
  fruit_images: [],
  pest_history: [],
  operation_history: [],
  prescription: null,
});

const filteredPlots = computed(() => {
  const q = keyword.value.trim().toLowerCase();
  if (!q) return plots.value;
  return plots.value.filter((item) =>
    `${item.block_id} ${item.plot_name || ""} ${item.crop_type || ""} ${item.status_label || ""} ${item.severity || ""}`
      .toLowerCase()
      .includes(q),
  );
});

// 电子药历：按日期+病种分组，每种病一个独立卡片
const FRUIT_TYPES = new Set(["猕猴桃", "橘子", "mihoutao", "juzi"]);
const imageCategoryOptions = ["未分类", "白粉病", "褐斑病", "菜青虫", "蜗牛", "褐斑病+菜青虫", "猕猴桃", "橘子"];
const timelineEntries = computed(() => {
  const groups = {};
  // 以图片为主线分组（图片自带 pest_type）
  for (const img of detail.images || []) {
    const d = (img.created_at || '').slice(0, 10);
    const pt = img.pest_type || 'unknown';
    if (!d || FRUIT_TYPES.has(pt)) continue;
    if (!groups[d]) groups[d] = { dateKey: d, pestMap: {}, ops: [] };
    if (!groups[d].pestMap[pt]) groups[d].pestMap[pt] = { pest_type: pt, sev: '正常', images: [], total: 0 };
    groups[d].pestMap[pt].images.push(img);
    groups[d].pestMap[pt].total++;
  }
  // 每种病害每天最多保留 10 张
  for (const d of Object.keys(groups)) {
    for (const pt of Object.keys(groups[d].pestMap)) {
      const blk = groups[d].pestMap[pt];
      if (blk.images.length > 10) {
        blk.images = blk.images.slice(0, 10);
      }
    }
  }
  // 根据当日图片张数判定严重度：≥3 张 → 严重，1-2 张 → 轻度，无图片时参考历史
  for (const d of Object.keys(groups)) {
    for (const pt of Object.keys(groups[d].pestMap)) {
      const blk = groups[d].pestMap[pt];
      if (blk.total >= 3) {
        blk.sev = '严重';
      } else if (blk.total >= 1) {
        blk.sev = '轻度';
      }
      // total == 0 保持 '正常'（仅历史记录无图片的情况）
    }
  }
  // 作业（过滤模拟机器人操作记录）
  for (const op of detail.operation_history || []) {
    if (op.operator === 'sim-robot-01') continue;
    const d = (op.created_at || '').slice(0, 10);
    if (!d) continue;
    if (!groups[d]) groups[d] = { dateKey: d, pestMap: {}, ops: [] };
    groups[d].ops.push({ type: 'op', ...op, label: `${op.action_type} · ${op.result || ''}` });
  }
  // 转为数组，pestMap → pestBlocks
  return Object.values(groups).map(g => ({
    ...g,
    pestBlocks: Object.values(g.pestMap).sort((a, b) => b.total - a.total),
  })).sort((a, b) => b.dateKey.localeCompare(a.dateKey));
});

const latestSeverity = computed(() => {
  const latestDay = timelineEntries.value[0];
  const latestPest = latestDay?.pestBlocks?.[0];
  if (latestPest?.sev) return latestPest.sev;
  const latestType = detail.latest?.pest_type || '';
  if (latestType && !FRUIT_TYPES.has(latestType) && latestType !== 'none') {
    return detail.latest?.severity || '-';
  }
  return '-';
});

// ---- 果实时间线（与病害并列展示） ----
const fruitTimelineEntries = computed(() => {
  const groups = {};
  for (const img of detail.fruit_images || []) {
    const d = (img.created_at || '').slice(0, 10);
    const pt = img.pest_type || '果实';
    if (!d) continue;
    if (!groups[d]) groups[d] = { dateKey: d, fruitMap: {} };
    if (!groups[d].fruitMap[pt]) groups[d].fruitMap[pt] = { pest_type: pt, images: [], total: 0 };
    groups[d].fruitMap[pt].images.push(img);
    groups[d].fruitMap[pt].total++;
  }
  // 每种果实每天最多保留 10 张
  for (const d of Object.keys(groups)) {
    for (const pt of Object.keys(groups[d].fruitMap)) {
      const blk = groups[d].fruitMap[pt];
      if (blk.images.length > 10) {
        blk.images = blk.images.slice(0, 10);
      }
    }
  }
  return Object.values(groups).map(g => ({
    ...g,
    fruitBlocks: Object.values(g.fruitMap).sort((a, b) => b.total - a.total),
  })).sort((a, b) => b.dateKey.localeCompare(a.dateKey));
});

// 待人工复核队列（从已有数据计算）
const showReviewBanner = ref(true);
const reviewItems = computed(() => {
  const a06 = plots.value.find((p) => p.block_id === "A-06");
  if (!a06 || !isReviewPhase.value) return [];
  return [{ block_id: "A-06", reason: "待人工复查", type: "phi" }];
});

// 缓存各区块处方中的锁定数量
const detailCache = reactive({});
async function cachePrescriptionLocks(blockId) {
  if (detailCache[blockId]) return;
  try {
    const rx = await getFarmPlotPrescription(blockId).catch(() => null);
    if (rx) {
      const locked = rx.recommendations?.filter(r => r.is_locked).length || 0;
      detailCache[blockId] = { locked_count: locked };
    }
  } catch (_) {}
}

let refreshTimer = null;
let detailRequestSeq = 0;

function formatNumber(value) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return "-";
  return Number(value).toFixed(1);
}

const insectKeywords = ['虫','蛾','蚜','虻','虱','螟','蟋','蝉','蜂','蚁','蝇','蝶','蚊','蝽','螨','螺','蜗牛','菜青虫','aphid','worm','moth','beetle','cricket','hopper','leafhopper','thrip','snail'];
function isInsect(name) {
  if (!name) return false;
  const lower = name.toLowerCase();
  return insectKeywords.some(k => lower.includes(k));
}

function resolveImageUrl(url) {
  if (!url) return "";
  if (/^https?:\/\//i.test(url)) return url;
  return `${apiBase}${url}`;
}

function resetDetail() {
  detail.plot = null;
  detail.latest = null;
  detail.images = [];
  detail.fruit_images = [];
  detail.pest_history = [];
  detail.operation_history = [];
  detail.prescription = null;
}

function closeDetail() {
  selectedBlockId.value = "";
  editMode.value = false;
  resetDetail();
}

async function moveImageDate(eventId, newDate) {
  if (!newDate) return;
  try {
    await patchDeviceEvent(eventId, { created_at: newDate + "T12:00:00" });
    // 刷新当前详情
    await openDetail(selectedBlockId.value, true);
  } catch (e) {
    errorText.value = "移动失败: " + e.message;
  }
}

async function updateImageCategory(eventId, pestType) {
  if (!pestType) return;
  try {
    savingCategoryEventId.value = eventId;
    const result = await patchDeviceEvent(eventId, { pest_type: pestType });
    const nextType = result?.pest_type || pestType;
    detail.images = (detail.images || []).map((img) =>
      img.event_id === eventId ? { ...img, pest_type: nextType } : img,
    );
    detail.fruit_images = (detail.fruit_images || []).map((img) =>
      img.event_id === eventId ? { ...img, pest_type: nextType } : img,
    );
    await openDetail(selectedBlockId.value, true);
  } catch (e) {
    errorText.value = "分类保存失败: " + e.message;
  } finally {
    savingCategoryEventId.value = "";
  }
}

async function deleteImage(eventId) {
  if (!confirm("确认删除这张图片？")) return;
  try {
    deletingEventId.value = eventId;
    await deleteDeviceEvent(eventId);
    await openDetail(selectedBlockId.value, true);
  } catch (e) {
    errorText.value = "删除失败: " + e.message;
  } finally {
    deletingEventId.value = "";
  }
}

async function loadFarmData() {
  if (loading.value) return;
  loading.value = true;
  errorText.value = "";
  try {
    const [statusData, plotsData] = await Promise.all([getFarmStatus(), getFarmPlots()]);
    summary.total = statusData.total ?? 0;
    summary.normal = statusData.normal ?? 0;
    summary.warning = statusData.warning ?? 0;
    summary.danger = statusData.danger ?? 0;
    summary.updated_at = statusData.updated_at ?? "";
    plots.value = plotsData.items ?? [];

    if (selectedBlockId.value) {
      const exists = plots.value.some((x) => x.block_id === selectedBlockId.value);
      if (!exists) {
        closeDetail();
      }
    }
  } catch (err) {
    errorText.value = "农田管理数据加载失败，请确认后端已启动。";
    console.error(err);
  } finally {
    loading.value = false;
  }
}

async function openDetail(blockId, background = false) {
  const requestId = ++detailRequestSeq;
  selectedBlockId.value = blockId;
  if (!background) {
    editMode.value = false;  // 仅主动打开时重置编辑模式，后台刷新不干扰
    recHeroExpanded.value = false;
  }
  
  if (!background) {
    detailLoading.value = true;
  }
  
  errorText.value = "";
  try {
    const data = await getFarmPlotDetail(blockId);
    if (requestId !== detailRequestSeq) return;
    detail.plot = data.plot ?? null;
    detail.latest = data.latest ?? null;
    detail.images = data.images ?? [];
    detail.fruit_images = data.fruit_images ?? [];
    detail.pest_history = data.pest_history ?? [];
    detail.operation_history = data.operation_history ?? [];

    const rxData = await getFarmPlotPrescription(blockId).catch(e => null);
    if (requestId !== detailRequestSeq) return;
    if(rxData) {
      detail.prescription = rxData;
    } else {
      detail.prescription = null;
    }
  } catch (err) {
    if (requestId !== detailRequestSeq) return;
    errorText.value = `区块 ${blockId} 详情加载失败。`;
    console.error(err);
  } finally {
    if (!background && requestId === detailRequestSeq) {
      detailLoading.value = false;
    }
  }
}

async function removePlot(blockId) {
  const ok = window.confirm(`确认删除区块 ${blockId} 的接收记录与历史记录吗？`);
  if (!ok) return;

  errorText.value = "";
  try {
    await clearFarmPlot(blockId);
    if (selectedBlockId.value === blockId) {
      closeDetail();
    }
    await loadFarmData();
  } catch (err) {
    errorText.value = `区块 ${blockId} 删除失败。`;
    console.error(err);
  }
}

onMounted(() => {
  loadFarmData();
  loadWeather();
  // 延迟预拉取处方锁定信息以填充复核队列
  setTimeout(() => { plots.value.forEach(p => cachePrescriptionLocks(p.block_id)); }, 800);
  refreshTimer = setInterval(() => {
    loadFarmData();
    if (selectedBlockId.value && !editMode.value) {
      openDetail(selectedBlockId.value, true);
    }
  }, 5000);

  // 监听模拟事件，立即刷新
  window.addEventListener('agri-sim-updated', onSimUpdated);
});

function onSimUpdated() {
  monitorPhase.value = sessionStorage.getItem('agri_sim_status') || 'scheduled';
  loadFarmData();
  if (selectedBlockId.value && !editMode.value) {
    openDetail(selectedBlockId.value, true);
  }
}

onBeforeUnmount(() => {
  window.removeEventListener('agri-sim-updated', onSimUpdated);
  if (refreshTimer) {
    clearInterval(refreshTimer);
    refreshTimer = null;
  }
});
</script>

<style scoped>
.farm-panel {
  display: flex;
  flex-direction: column;
  gap: 12px;
  height: 100%;
  min-height: 0;
}

.farm-panel.detail-mode {
  gap: 0;
}

.farm-card {
  background: rgba(255, 255, 255, 0.94);
  border: 1px solid #e3ece5;
  border-radius: 16px;
  box-shadow: 0 8px 24px rgba(13, 78, 39, 0.08);
}

.farm-status-grid {
  display: grid;
  gap: 10px;
  grid-template-columns: repeat(4, minmax(0, 1fr));
}

.status-card {
  padding: 12px 14px;
}

.status-card.total {
  border-left: 4px solid #1976d2;
}

.status-card.normal {
  border-left: 4px solid #3aaa35;
}

.status-card.warning {
  border-left: 4px solid #f6a100;
}

.status-card.danger {
  border-left: 4px solid #ff4d6d;
}

.status-title {
  margin: 0;
  color: #6a7f6c;
  font-size: 0.86rem;
}

.status-value {
  margin: 8px 0 0;
  font-size: 2rem;
  font-weight: 700;
  color: #1d5f31;
}

.farm-main-grid {
  display: grid;
  grid-template-columns: 1fr;
  gap: 10px;
  min-height: 0;
}

.detail-only-wrap {
  flex: 1;
  min-height: 0;
}

.detail-only-card {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.table-card,
.detail-card {
  padding: 12px;
  min-height: 0;
}

.table-top {
  display: flex;
  justify-content: space-between;
  gap: 8px;
  align-items: center;
  margin-bottom: 10px;
  flex-wrap: wrap;
}

.table-top h3,
.detail-top h3,
.block-section h4 {
  margin: 0;
  color: #205d35;
}

.table-top p,
.detail-top p {
  margin: 6px 0 0;
  color: #628168;
  font-size: 0.84rem;
}

.table-tools {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-left: auto;
}

.search-input {
  width: min(320px, 54vw);
  border: 1px solid #cfe1d3;
  border-radius: 8px;
  padding: 7px 10px;
  font-size: 0.82rem;
  outline: none;
}

.search-input:focus {
  border-color: #4caf50;
  box-shadow: 0 0 0 3px rgba(76, 175, 80, 0.15);
}

.primary-btn,
.secondary-btn,
.danger-btn {
  border: none;
  border-radius: 8px;
  padding: 7px 10px;
  color: #fff;
  font-size: 0.8rem;
}

.primary-btn {
  background: #2e7d32;
}

.secondary-btn {
  background: #1f78d1;
}

.danger-btn {
  background: #d64545;
}

.edit-btn {
  border: none;
  border-radius: 8px;
  padding: 7px 10px;
  color: #fff;
  font-size: 0.8rem;
  background: #2e7d32;
}

.edit-input {
  width: 100%;
  padding: 4px 6px;
  border: 1px solid #4caf50;
  border-radius: 6px;
  font-size: 0.8rem;
  background: #f1f8e9;
}

.col-time {
  text-align: center;
}

.col-status {
  text-align: center;
}

.status-pending {
  color: #e65100;
  font-weight: 600;
  font-size: 0.78rem;
}

.status-review {
  color: #f9a825;
  font-weight: 600;
  font-size: 0.78rem;
}

.status-mild {
  color: #d9bf63;
  font-weight: 600;
  font-size: 0.78rem;
}

.status-running {
  color: #1565c0;
  font-weight: 600;
  font-size: 0.78rem;
  animation: pulse-blue 1.2s infinite;
}
@keyframes pulse-blue {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.55; }
}

.status-done {
  color: #2e7d32;
  font-size: 0.78rem;
}

.primary-btn:disabled {
  opacity: 0.6;
}

.table-wrap {
  font-size: 0.83rem;
  overflow: auto;
  max-height: 60vh;
}

.thead,
.tbody-row {
  display: grid;
  grid-template-columns: 0.35fr 0.5fr 0.8fr 0.62fr 0.78fr 1.05fr 0.8fr;
  gap: 6px;
  align-items: center;
}

.thead {
  padding: 8px 0;
  font-weight: 700;
  color: #547459;
  border-bottom: 1px solid #e0ebe3;
}

.tbody-row {
  padding: 8px 0;
  border-bottom: 1px solid #edf4ee;
}

.tbody-row.selected {
  background: rgba(53, 134, 69, 0.08);
}

.tbody-row.danger {
  background: rgba(244, 67, 54, 0.08);
}

.actions {
  display: flex;
  gap: 6px;
}

.table-state,
.detail-empty {
  padding: 16px 8px;
  text-align: center;
  color: #6a816c;
}

.detail-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
}

.detail-sub-title {
  margin: 6px 0 0;
  color: #628168;
  font-size: 0.84rem;
}

.close-btn {
  border: 1px solid #c9dbcd;
  background: #f3f8f4;
  color: #2f5f3a;
  border-radius: 8px;
  padding: 6px 10px;
  font-size: 0.78rem;
}

.close-btn:hover {
  background: #e7f1e9;
}

.detail-top-actions {
  display: flex; gap: 8px; align-items: center;
}
.edit-mode-btn {
  border: 1px solid #c9a34a;
  background: #fdf6e8;
  color: #8b6914;
  border-radius: 8px;
  padding: 6px 12px;
  font-size: 0.78rem;
  cursor: pointer;
}
.edit-mode-btn:hover { background: #f9ecc8; }
.edit-mode-btn.active {
  background: #4caf50; color: #fff; border-color: #4caf50;
}

.emr-thumb.editing { position: relative; padding-bottom: 52px; }
.emr-thumb.editing img {
  pointer-events: none;
}
.emr-type-edit {
  position: absolute; bottom: 24px; left: 4px; right: 26px;
  font-size: 0.65rem; padding: 1px 3px;
  border: 1px solid #ccc; border-radius: 4px;
  background: #fff;
  z-index: 3;
  pointer-events: auto;
}
.emr-type-edit:disabled {
  opacity: 0.55;
  cursor: wait;
}
.emr-date-edit {
  position: absolute; bottom: 2px; left: 4px; right: 26px;
  font-size: 0.65rem; padding: 1px 3px;
  border: 1px solid #ccc; border-radius: 4px;
  background: #fff;
  z-index: 3;
}
.emr-del-btn {
  position: absolute; top: -4px; right: -4px;
  width: 22px; height: 22px; padding: 0;
  border-radius: 50%; border: 1px solid #e88;
  background: #fff; color: #c33;
  font-size: 0.85rem; font-weight: bold; line-height: 20px;
  cursor: pointer;
  text-align: center;
  z-index: 4;
}
.emr-del-btn:hover { background: #fdd; }
.emr-del-btn:disabled {
  opacity: 0.55;
  cursor: wait;
}

.detail-content {
  display: flex;
  flex-direction: column;
  gap: 12px;
  min-height: 0;
  max-height: calc(100vh - 240px);
  overflow: auto;
}

.detail-only-card .detail-content {
  flex: 1;
  max-height: none;
}

.latest-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
}

.latest-item {
  border: 1px solid #dfebe1;
  border-radius: 10px;
  padding: 8px;
  background: #f7fbf8;
}

.latest-item label {
  display: block;
  color: #64806a;
  font-size: 0.75rem;
}

.latest-item strong {
  color: #1c5131;
  font-size: 0.9rem;
}

.latest-item strong.red {
  color: #c62828;
}

.block-section {
  border-top: 1px solid #e7efe8;
  padding-top: 10px;
}

.muted {
  color: #6b7f6e;
  margin: 8px 0 0;
  font-size: 0.84rem;
}

.image-grid {
  margin-top: 8px;
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 8px;
}

.image-item {
  border: 1px solid #dce9de;
  border-radius: 10px;
  overflow: hidden;
  text-decoration: none;
  background: #fff;
}

.image-item img {
  width: 100%;
  height: 110px;
  object-fit: cover;
  display: block;
}

.image-item span {
  display: block;
  padding: 6px 8px;
  color: #5f7462;
  font-size: 0.76rem;
}

/* ===== 电子药历时间轴 ===== */
.emr-timeline { display: flex; flex-direction: column; gap: 0; }
.emr-day { position: relative; padding-left: 22px; }
.emr-day:not(:last-child) { padding-bottom: 14px; border-left: 2px solid rgba(59,107,59,0.12); }

.emr-date-head { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; }
.emr-date-dot { position: absolute; left: -6px; color: #3B6B3B; font-size: 1rem; line-height: 1; }
.emr-date-text { font-weight: 700; font-size: 0.82rem; color: #2d5a27; }
.emr-date-line { flex: 1; height: 1px; background: rgba(59,107,59,0.1); }

.emr-day-body {
  background: rgba(255,255,255,0.55);
  border: 1px solid rgba(59,107,59,0.06);
  border-radius: 10px;
  padding: 10px;
  display: flex; flex-direction: column; gap: 8px;
}

/* 事件子框 — 实拍+病虫害合在一起 */
.emr-event-block {
  background: rgba(255,255,255,0.7);
  border: 1px solid rgba(59,107,59,0.08);
  border-radius: 8px; padding: 8px 10px;
  display: flex; flex-direction: column; gap: 8px;
}
.emr-event-time {
  font-size: 0.65rem; color: #8a9e7f; letter-spacing: 0.3px;
}

/* 实拍缩略图 — 比例自适应 + 可点击 */
.emr-pest-card {
  background: rgba(255,255,255,0.7);
  border: 1px solid rgba(59,107,59,0.08);
  border-radius: 10px; padding: 10px 12px;
  display: flex; flex-direction: column; gap: 8px;
}
.emr-pest-card-hd {
  display: flex; align-items: center; gap: 8px;
  font-size: 0.82rem; font-weight: 600; color: #2d4a22;
}
.emr-card-dot { width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0; }
.dot-red  { background: #ef5350; }
.dot-amber { background: #ff9800; }
.dot-green { background: #8bc34a; }
.emr-card-title { flex: 0 0 auto; }
.emr-card-sev { font-size: 0.7rem; font-weight: 500; }
.sev-red   { color: #c62828; }
.sev-amber { color: #e65100; }
.sev-green { color: #558b2f; }
.emr-card-count { font-size: 0.68rem; color: #8a9e7f; font-weight: 400; }

.emr-image-row { display: flex; gap: 10px; flex-wrap: wrap; }
.emr-thumb {
  width: 130px; border: 1px solid rgba(59,107,59,0.1); border-radius: 8px;
  overflow: hidden; cursor: pointer; background: #fff;
  transition: transform 0.2s;
}
.emr-thumb:hover { transform: scale(1.03); }
.emr-thumb img {
  width: 100%; height: auto; display: block;
  object-fit: contain; max-height: 140px;
}

/* ===== 图片灯箱 ===== */
.lightbox-overlay {
  position: fixed; inset: 0; background: rgba(0,0,0,0.65); z-index: 200;
  display: flex; align-items: center; justify-content: center;
  backdrop-filter: blur(8px);
}
.lightbox-close {
  position: absolute; top: 20px; right: 20px; z-index: 3;
  border: none; background: rgba(255,255,255,0.2); border-radius: 50%;
  width: 40px; height: 40px; display: flex; align-items: center; justify-content: center;
  color: #fff; cursor: pointer;
}
.lightbox-img {
  max-width: 90vw; max-height: 85vh; object-fit: contain;
  border-radius: 8px; box-shadow: 0 16px 48px rgba(0,0,0,0.3);
}
.lightbox-prev, .lightbox-next {
  position: absolute; top: 50%; transform: translateY(-50%);
  border: none; background: rgba(255,255,255,0.15); border-radius: 50%;
  width: 48px; height: 48px; display: flex; align-items: center; justify-content: center;
  color: #fff; cursor: pointer; z-index: 3; transition: background 0.2s;
}
.lightbox-prev:hover, .lightbox-next:hover { background: rgba(255,255,255,0.3); }
.lightbox-prev { left: 20px; }
.lightbox-next { right: 20px; }
.lightbox-counter {
  position: absolute; bottom: 30px; left: 50%; transform: translateX(-50%);
  color: #fff; font-size: 0.85rem; background: rgba(0,0,0,0.4);
  padding: 4px 16px; border-radius: 12px; z-index: 3;
}

/* 病虫害 / 作业 芯片 */
.emr-chips { display: flex; flex-wrap: wrap; gap: 6px; }
.emr-chip {
  display: inline-flex; align-items: center; gap: 4px;
  padding: 4px 10px; border-radius: 6px; font-size: 0.74rem;
}
.emr-chip.pest { background: #fff3e0; border: 1px solid #ffe0b2; }
.emr-chip.pest.mild { background: #f1f8e9; border-color: #c8e6c9; }
.emr-chip.pest.severe { background: #ffebee; border-color: #ffcdd2; }
.emr-chip.op { background: #e8f5e9; border: 1px solid #c8e6c9; }

.emr-chip-icon { font-size: 0.75rem; }
.emr-chip-tag { font-size: 0.6rem; color: #8a9e7f; margin-right: 2px; }
.emr-chip-name { font-weight: 600; color: #2d5a27; }
.emr-chip-sev { font-size: 0.65rem; }
.sev-red { color: #c62828; }
.sev-amber { color: #e65100; }
.sev-green { color: #2e7d32; }

.history-list {
  margin: 8px 0 0;
  padding: 0;
  list-style: none;
  display: grid;
  gap: 8px;
}

.history-list li {
  border: 1px solid #dfebe1;
  border-radius: 10px;
  padding: 8px;
  background: #f8fbf9;
}

.history-list strong {
  display: block;
  color: #265d39;
  font-size: 0.82rem;
}

.history-list span {
  display: block;
  margin-top: 4px;
  color: #607763;
  font-size: 0.8rem;
}

.error-line {
  margin: 0;
  color: #c62828;
  font-size: 0.84rem;
}

/* 待人工复核横幅 */
.review-banner {
  position: relative;
  z-index: 2;
  background: linear-gradient(135deg, #fff8e1, #fff3e0);
  border: 1px solid #ffcc80;
  border-radius: 12px;
  overflow: visible;
  transition: all 0.3s;
}
.review-banner.collapsed { border-color: #ffe0b2; }

.review-banner-head {
  display: flex; align-items: center; gap: 8px;
  padding: 10px 14px; cursor: pointer; user-select: none;
}
.review-banner-head:hover { background: rgba(255,152,0,0.04); }

.review-banner-icon { font-size: 1rem; }
.review-banner-title { font-weight: 600; color: #e65100; font-size: 0.85rem; flex: 1; }
.review-banner-arrow { color: #bf360c; font-size: 0.75rem; }

.review-banner-body {
  display: flex; flex-wrap: wrap; gap: 8px;
  padding: 0 14px 12px;
}

.review-chip {
  display: inline-flex; align-items: center; gap: 6px;
  padding: 6px 12px; border-radius: 8px; cursor: pointer;
  font-size: 0.78rem; transition: transform 0.15s;
}
.review-chip:hover { transform: translateY(-1px); }

.review-chip.danger { background: #ffebee; border: 1px solid #ffcdd2; }
.review-chip.warn  { background: #fff3e0; border: 1px solid #ffe0b2; }
.review-chip.phi   { background: #fce4ec; border: 1px solid #f8bbd0; }

.review-chip-id { font-weight: 700; color: #2d5a27; }
.review-chip.danger .review-chip-reason { color: #c62828; }
.review-chip.warn  .review-chip-reason { color: #e65100; }
.review-chip.phi   .review-chip-reason { color: #ad1457; }

.review-chip-action { color: #8d6e63; font-size: 0.7rem; margin-left: 4px; }

.ai-presc-section {
  background: #f1f8e9;
  border-radius: 8px;
  padding: 12px;
  border: 1px solid #c5e1a5;
  margin-bottom: 16px;
}

.warnings {
  margin-bottom: 16px;
}

.warning-box {
  background: #fff3e0;
  border-left: 4px solid #ff9800;
  padding: 10px 14px;
  font-size: 0.85rem;
  border-radius: 4px;
  color: #e65100;
  margin-bottom: 8px;
  font-weight: 600;
}

.timeline {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin: 16px 0 20px;
  padding-left: 8px;
  border-left: 2px solid #e0e0e0;
}

.timeline-item {
  position: relative;
  padding-left: 16px;
}

.timeline-item .dot {
  position: absolute;
  left: -21px;
  top: 4px;
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: #bdbdbd;
  border: 2px solid #fff;
}

.timeline-item.past .dot {
  background: #9e9e9e;
}
.timeline-item.current .dot {
  background: #2196f3;
  box-shadow: 0 0 0 2px rgba(33, 150, 243, 0.3);
}
.timeline-item.future .dot {
  background: #ffb300;
}

.timeline-item .content {
  display: flex;
  flex-direction: column;
  font-size: 0.85rem;
}
.timeline-item .date {
  color: #607763;
  font-family: monospace;
}
.timeline-item .event {
  font-weight: 500;
  color: #265d39;
}

.recommendations {
  display: grid;
  gap: 12px;
}

.ai-title {
  margin: 0 0 8px;
  font-size: 1rem;
  color: #2d5a27;
  display: flex;
  align-items: center;
  gap: 6px;
}
.ai-title-icon { font-size: 0.9rem; }
.ai-toggle {
  margin-left: auto;
  border: none;
  background: rgba(59,107,59,0.08);
  color: #3B6B3B;
  font-size: 0.7rem;
  padding: 3px 10px;
  border-radius: 4px;
  cursor: pointer;
}

/* 推荐方案 Hero 区 */
.rec-hero {
  background: linear-gradient(135deg, #e8f5e9, #f1f8e9);
  border: 1px solid #c8e6c9;
  border-radius: 10px;
  padding: 12px 14px;
  margin-top: 10px;
}
.rec-hero-label { font-weight: 700; font-size: 0.82rem; color: #2e7d32; }
.rec-hero-bar { display: flex; align-items: center; justify-content: space-between; margin-bottom: 8px; }
.rec-hero-toggle {
  border: none; background: rgba(59,107,59,0.07); color: #3B6B3B;
  font-size: 0.68rem; padding: 2px 8px; border-radius: 4px; cursor: pointer;
}
.rec-hero-hint { margin: 0; font-size: 0.76rem; color: #8a9e7f; }
.rec-card.recommended { background: #fff; border: 1px solid #a5d6a7; box-shadow: 0 2px 8px rgba(46,125,50,0.06); }

/* 已排除方案 — 折叠 */
.rec-excluded {
  margin-top: 12px;
  border: 1px solid rgba(198,40,40,0.12);
  border-radius: 8px;
  overflow: hidden;
}
.rec-excluded-toggle {
  width: 100%; border: none; background: rgba(198,40,40,0.04);
  display: flex; align-items: center; justify-content: space-between;
  padding: 8px 12px; font-size: 0.78rem; color: #c62828; cursor: pointer;
}
.rec-excluded-toggle:hover { background: rgba(198,40,40,0.07); }
.rec-excluded-arrow { font-size: 0.7rem; }
.rec-excluded-body { padding: 10px; display: flex; flex-direction: column; gap: 8px; background: rgba(255,235,238,0.2); }
.rec-excluded .rec-card.locked { background: #fff; border: 1px solid #ffcdd2; margin: 0; }

.rec-card { border-radius: 8px; padding: 10px 14px; }
.rec-card.locked { background: #fff5f5; border: 1px solid #ffcdd2; }
.rec-card.warn { background: #fffde7; border: 1px solid #fff9c4; }
.rec-header { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.rec-header h5 { margin: 0; font-size: 0.86rem; color: #2d5a27; }
.tag { font-size: 0.64rem; padding: 2px 8px; border-radius: 4px; background: rgba(239,83,80,0.1); color: #c62828; font-weight: 600; }
.rec-body { margin-top: 6px; }
.rec-body p { margin: 0; font-size: 0.74rem; color: #6b8a65; }
.rec-body .reason { margin-top: 4px; font-size: 0.72rem; color: #8d6e63; }
.rec-card.recommended .rec-body .reason { color: #2d5a27; font-weight: 500; }

.rec-body .reason {
  margin-top: 6px;
  font-weight: 600;
  color: #424242;
}

@media (max-width: 1200px) {
  .farm-main-grid {
    grid-template-columns: 1fr;
  }

  .farm-status-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 700px) {
  .farm-status-grid {
    grid-template-columns: 1fr;
  }

  .table-tools {
    width: 100%;
    margin-left: 0;
  }

  .search-input {
    width: 100%;
  }

  .latest-grid,
  .image-grid {
    grid-template-columns: 1fr;
  }
}

/* 展开/收起切换按钮 */
.section-toggle-h4 {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin: 0;
  color: #205d35;
}
.toggle-btn {
  border: 1px solid #c9dbcd;
  background: #f3f8f4;
  color: #2f5f3a;
  border-radius: 6px;
  padding: 2px 10px;
  font-size: 0.72rem;
  cursor: pointer;
  transition: background 0.2s;
}
.toggle-btn:hover {
  background: #e7f1e9;
}
</style>
