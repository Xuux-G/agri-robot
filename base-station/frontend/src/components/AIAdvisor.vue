<script setup>
import { ref, computed } from "vue";
import { askAIAdvise } from "../api/client.js";

const emit = defineEmits(["viewPest"]);

const question = ref("");
const answer = ref("");
const loading = ref(false);
const error = ref("");

const knownPests = ["白粉病", "褐斑病", "黑斑病", "白斑病", "株腐病", "霜霉病", "灰霉病", "枯萎病", "炭疽病", "叶斑病", "根腐病", "茎枯病", "低温障碍", "病毒病", "蚜虫", "菜青虫", "蜗牛", "甜菜夜蛾", "蓟马", "红蜘蛛", "潜叶蝇", "油葫芦"];

const foundPests = computed(() => {
  if (!answer.value) return [];
  return knownPests.filter(p => answer.value.includes(p));
});

function goToPest(name) {
  emit("viewPest", name);
}

async function askAI() {
  if (!question.value.trim()) return;
  loading.value = true;
  error.value = "";
  answer.value = "";
  try {
    const data = await askAIAdvise(question.value);
    answer.value = data.answer || "未获取到回复";
  } catch (e) {
    error.value = "AI 请求失败: " + e.message;
  } finally {
    loading.value = false;
  }
}
</script>

<template>
  <div class="ai-advisor">
    <div class="ai-header">
      <h3>AI 植保顾问</h3>
      <p>输入地块当前情况，基于本站知识库智能推荐施药方案</p>
    </div>
    <div class="ai-chat">
      <textarea
        v-model="question"
        placeholder="例如：A-03地块番茄发现白粉病，且前次施药距今已过8天..."
        rows="3"
      ></textarea>
      <button :disabled="loading || !question.trim()" @click="askAI">
        {{ loading ? "分析中..." : "咨询 AI" }}
      </button>
    </div>
    <div v-if="error" class="ai-error">{{ error }}</div>
    <div v-if="answer" class="ai-answer">
      <div class="ai-answer-label">AI 建议</div>
      <div class="ai-answer-text" v-text="answer"></div>
      <div v-if="foundPests.length" class="ai-actions">
        <span class="ai-actions-label">查看知识库详情：</span>
        <button
          v-for="pest in foundPests" :key="pest"
          class="ai-detail-btn"
          @click="goToPest(pest)"
        >{{ pest }} 详细 →</button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.ai-advisor {
  margin-top: 24px;
  background: rgba(255,255,255,0.7);
  border: 1px solid rgba(59,107,59,0.12);
  border-radius: 14px;
  padding: 18px;
}
.ai-header h3 { margin: 0; font-size: 1rem; color: #2d5a27; }
.ai-header p { margin: 4px 0 0; font-size: 0.78rem; color: #7a9670; }
.ai-chat { margin-top: 12px; display: flex; flex-direction: column; gap: 10px; }
.ai-chat textarea {
  border: 1px solid rgba(59,107,59,0.15); border-radius: 8px;
  padding: 10px 12px; font-size: 0.82rem; outline: none;
  background: rgba(255,255,255,0.7); color: #3a5530; resize: vertical;
  font-family: var(--font-stack);
}
.ai-chat textarea:focus { border-color: #7CB342; }
.ai-chat button {
  align-self: flex-end; border: none; background: #3B6B3B; color: #fff;
  padding: 8px 20px; border-radius: 8px; font-size: 0.82rem; cursor: pointer;
}
.ai-chat button:disabled { opacity: 0.6; cursor: not-allowed; }
.ai-error { margin-top: 10px; color: #c62828; font-size: 0.8rem; }
.ai-answer { margin-top: 14px; padding: 14px; background: rgba(232,245,233,0.6); border-radius: 10px; border: 1px solid rgba(59,107,59,0.1); }
.ai-answer-label { font-size: 0.78rem; font-weight: 600; color: #2d5a27; margin-bottom: 6px; }
.ai-answer-text { font-size: 0.82rem; color: #4a6740; line-height: 1.7; white-space: pre-wrap; }
.ai-actions { margin-top: 12px; display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.ai-actions-label { font-size: 0.76rem; color: #7a9670; }
.ai-detail-btn {
  border: 1px solid #3B6B3B; background: #fff;
  color: #3B6B3B; padding: 4px 12px; border-radius: 6px;
  font-size: 0.76rem; cursor: pointer;
}
.ai-detail-btn:hover { background: #3B6B3B; color: #fff; }
</style>
