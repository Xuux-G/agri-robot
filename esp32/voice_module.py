# 语音模块 MicroPython 驱动库
# 对应烧录固件的完整词条表
# 协议格式: AA 55 <cmd> 00 FB

from machine import UART, Pin
from time import sleep_ms

class VoiceModule:
    '''CI1302 语音模块控制类
    
    用法:
        vm = VoiceModule(uart_id=1, tx=Pin(35), rx=Pin(36))
        vm.increase_volume()
        vm.detect_block(1)
        vm.say_crop('柑橘')
    '''
    
    CMD = {
        # ── 音量控制 ──
        'INCREASE_VOLUME': 0x01,  # 增加音量
        'DECREASE_VOLUME': 0x02,  # 减小音量
        'MAX_VOLUME':      0x03,  # 最大音量
        'MEDIUM_VOLUME':   0x04,  # 中等音量
        
        # ── 唤醒词回复 ──
        'WELCOME':         0x05,  # "我在"(唤醒词"你好小车"的回复)
        'CAR_CAR':         0x1F,  # "小车小车"(另一个唤醒词)
        
        # ── 区块检测 ──
        'BLOCK1':          0x06,  # 检测到区块一
        'BLOCK2':          0x07,  # 检测到区块二
        'BLOCK3':          0x08,  # 检测到区块三
        'BLOCK4':          0x09,  # 检测到区块四
        'BLOCK5':          0x0A,  # 检测到区块五
        'BLOCK6':          0x0B,  # 检测到区块六
        
        # ── 作物识别 ──
        'CROP_CITRUS':     0x0C,  # 当前区块作为柑橘
        'CROP_KIWI':       0x0D,  # 当前区块作为猕猴桃
        'CROP_SOYBEAN':    0x0E,  # 当前区块作为大豆
        'CROP_CAMELLIA':   0x10,  # 当前区块作为山茶花
        'CROP_POMEGRANATE':0x11, # 当前区块作为石榴
        'CROP_PEPPER':     0x12,  # 当前区块作为辣椒
        'CROP_STRAWBERRY': 0x13, # 当前区块作为草莓
        'CROP_TOMATO':     0x14,  # 当前区块作为番茄
        'CROP_TEA':        0x15,  # 当前区块作为茶树
        
        # ── 智能分析 ──
        'HISTORY_JUDGE':   0x16,  # 综合历史判断
        'AI_ANALYSIS':     0x17,  # ai智能分析得
        
        # ── 病害防治 ──
        'TREAT_FUNGUS':    0x18,  # 我现在需要治真菌病 → 应喷撒撒吡唑醚菌酯
        'PROTECT_BACTERIA':0x19, # 我现在需要保护性杀菌 → 应喷撒代森锰锌
        'KILL_MITE':       0x1A,  # 我现在需要杀虫杀螨 → 应喷撒阿维菌素
        'NO_PEST':         0x1B,  # 未发现病虫害 → 应喷撒芸苔素内酯

        # ── 新增病虫害语音协议 ──
        'TREAT_SNAIL':     0x1E,  # 应喷洒杀螺胺乙醇胺盐悬
        'DETECT_CAIQINGCHONG': 0x20,  # 检测到菜青虫
        'DETECT_BAIFEN':   0x21,  # 检测到白粉病
        'DETECT_HEBAN':    0x22,  # 检测到褐斑病
        'DETECT_HEBAN_CAIQINGCHONG': 0x23,  # 检测到褐斑病与菜青虫
        'DETECT_WONIU':    0x24,  # 检测到蜗牛
        'TREAT_HEBAN_CAIQINGCHONG': 0x25,  # 应喷洒吡唑醚菌酯与阿维菌素
        'NO_PEST_FOUND':   0x26,  # 未发现病虫害
        'MANUAL_RECHECK':  0x27,  # 状态存疑待人工复查
        
        # ── 任务管理 ──
        'TASK_DONE':       0x1C,  # 任务完成 → 喷洒完成
        'INACTIVATE':      0x1D,  # 关闭播报 → 本次任务完成
    }
    
    def __init__(self, uart_id=1, tx=None, rx=None, baudrate=115200):
        '''初始化语音模块
        
        参数:
            uart_id: UART编号 (默认1)
            tx: TX引脚 (默认GPIO15)
            rx: RX引脚 (默认GPIO16)
            baudrate: 波特率 (默认115200)
        '''
        if tx is None:
            tx = Pin(15)
        elif isinstance(tx, int):
            tx = Pin(tx)

        if rx is None:
            rx = Pin(16)
        elif isinstance(rx, int):
            rx = Pin(rx)

        self.uart = UART(uart_id, baudrate=baudrate, tx=tx, rx=rx)
        self.uart.init(baudrate=baudrate, bits=8, parity=None, stop=1)
    
    def _send(self, cmd_byte):
        '''发送命令字节到语音模块'''
        buf = bytearray([0xAA, 0x55, cmd_byte, 0x00, 0xFB])
        self.uart.write(buf)
        sleep_ms(10)  # 等待播报
    
    # ═══════════════════════════════════════
    #  音量控制
    # ═══════════════════════════════════════
    
    def increase_volume(self):
        '''增加音量'''
        self._send(self.CMD['INCREASE_VOLUME'])
    
    def decrease_volume(self):
        '''减小音量'''
        self._send(self.CMD['DECREASE_VOLUME'])
    
    def max_volume(self):
        '''最大音量'''
        self._send(self.CMD['MAX_VOLUME'])
    
    def medium_volume(self):
        '''中等音量'''
        self._send(self.CMD['MEDIUM_VOLUME'])
    
    # ═══════════════════════════════════════
    #  唤醒词（需先喊"你好小车"或"小车小车"）
    # ═══════════════════════════════════════
    
    def welcome(self):
        '''播报"我在"(唤醒词回复)'''
        self._send(self.CMD['WELCOME'])
    
    def car_car(self):
        '''播报"小车小车"(另一个唤醒词)'''
        self._send(self.CMD['CAR_CAR'])
    
    # ═══════════════════════════════════════
    #  区块检测
    # ═══════════════════════════════════════
    
    def detect_block(self, n):
        '''检测到区块n (n支持1-6)
        
        示例:
            vm.detect_block(1)  → "检测到区块一"
            vm.detect_block(6)  → "检测到区块六"
        '''
        key = f'BLOCK{n}'
        if key in self.CMD:
            self._send(self.CMD[key])
        else:
            raise ValueError(f'区块编号无效: {n} (支持1-6)')
    
    # ═══════════════════════════════════════
    #  作物识别
    # ═══════════════════════════════════════
    
    def _crop_name_to_key(self, crop_name):
        mapping = {
            '柑橘': 'CROP_CITRUS',
            '猕猴桃': 'CROP_KIWI',
            '大豆': 'CROP_SOYBEAN',
            '山茶花': 'CROP_CAMELLIA',
            '石榴': 'CROP_POMEGRANATE',
            '辣椒': 'CROP_PEPPER',
            '草莓': 'CROP_STRAWBERRY',
            '番茄': 'CROP_TOMATO',
            '茶树': 'CROP_TEA',
        }
        key = mapping.get(crop_name)
        if key is None:
            raise ValueError(f'未知作物: {crop_name}')
        return key
    
    def say_crop(self, crop_name):
        '''播报当前区块作为某作物
        
        示例:
            vm.say_crop('柑橘')    → "当前区块作为柑橘"
            vm.say_crop('草莓')    → "当前区块作为草莓"
        '''
        key = self._crop_name_to_key(crop_name)
        self._send(self.CMD[key])
    
    # ═══════════════════════════════════════
    #  智能分析
    # ═══════════════════════════════════════
    
    def history_judge(self):
        '''综合历史判断'''
        self._send(self.CMD['HISTORY_JUDGE'])
    
    def ai_analysis(self):
        '''ai智能分析得'''
        self._send(self.CMD['AI_ANALYSIS'])
    
    # ═══════════════════════════════════════
    #  病害防治建议
    # ═══════════════════════════════════════
    
    def treat_fungus(self):
        '''我现在需要治真菌病 → 应喷撒撒吡唑醚菌酯'''
        self._send(self.CMD['TREAT_FUNGUS'])
    
    def protect_bacteria(self):
        '''我现在需要保护性杀菌 → 应喷撒代森锰锌'''
        self._send(self.CMD['PROTECT_BACTERIA'])
    
    def kill_mite(self):
        '''我现在需要杀虫杀螨 → 应喷撒阿维菌素'''
        self._send(self.CMD['KILL_MITE'])
    
    def no_pest(self):
        '''未发现病虫害 → 应喷撒芸苔素内酯'''
        self._send(self.CMD['NO_PEST'])

    def treat_snail(self):
        '''应喷洒杀螺胺乙醇胺盐悬'''
        self._send(self.CMD['TREAT_SNAIL'])

    def detect_caiqingchong(self):
        '''检测到菜青虫'''
        self._send(self.CMD['DETECT_CAIQINGCHONG'])

    def detect_baifen(self):
        '''检测到白粉病'''
        self._send(self.CMD['DETECT_BAIFEN'])

    def detect_heban(self):
        '''检测到褐斑病'''
        self._send(self.CMD['DETECT_HEBAN'])

    def detect_heban_caiqingchong(self):
        '''检测到褐斑病与菜青虫'''
        self._send(self.CMD['DETECT_HEBAN_CAIQINGCHONG'])

    def detect_woniu(self):
        '''检测到蜗牛'''
        self._send(self.CMD['DETECT_WONIU'])

    def treat_heban_caiqingchong(self):
        '''应喷洒吡唑醚菌酯与阿维菌素'''
        self._send(self.CMD['TREAT_HEBAN_CAIQINGCHONG'])

    def no_pest_found(self):
        '''未发现病虫害'''
        self._send(self.CMD['NO_PEST_FOUND'])

    def manual_recheck(self):
        '''状态存疑待人工复查'''
        self._send(self.CMD['MANUAL_RECHECK'])

    def play_sequence(self, cmd_list, gap_ms=600):
        '''按顺序连续播报多个命令'''
        for index, cmd in enumerate(cmd_list):
            self._send(cmd)
            if index != len(cmd_list) - 1:
                sleep_ms(gap_ms)

    def play_fixed_diagnosis(self, block_id):
        '''按固定区块规则播报识别结果与处置建议'''
        sequences = {
            1: ['DETECT_CAIQINGCHONG', 'AI_ANALYSIS', 'KILL_MITE'],
            2: ['DETECT_BAIFEN', 'AI_ANALYSIS', 'TREAT_FUNGUS'],
            3: ['DETECT_HEBAN_CAIQINGCHONG', 'AI_ANALYSIS', 'TREAT_HEBAN_CAIQINGCHONG'],
            4: ['DETECT_WONIU', 'AI_ANALYSIS', 'TREAT_SNAIL'],
            5: ['NO_PEST_FOUND', 'HISTORY_JUDGE', 'NO_PEST'],
            6: ['DETECT_HEBAN', 'AI_ANALYSIS', 'MANUAL_RECHECK'],
        }
        keys = sequences.get(block_id)
        if not keys:
            raise ValueError(f'未知区块: {block_id}')
        self.play_sequence([self.CMD[key] for key in keys])
    
    # ═══════════════════════════════════════
    #  任务管理
    # ═══════════════════════════════════════
    
    def task_done(self):
        '''任务完成 → 喷洒完成'''
        self._send(self.CMD['TASK_DONE'])
    
    def inactivate(self):
        '''关闭播报 → 本次任务完成'''
        self._send(self.CMD['INACTIVATE'])
    
    # ═══════════════════════════════════════
    #  通用接口
    # ═══════════════════════════════════════
    
    def play(self, cmd_id):
        '''通过命令ID直接播报
        
        参数:
            cmd_id: 命令字节 (0x01-0x1F)
            
        示例:
            vm.play(0x06)  # 播报"检测到区块一"
        '''
        self._send(cmd_id)
    
    def send_raw(self, data):
        '''发送原始数据
        
        参数:
            data: bytes或bytearray，完整的5字节命令
        '''
        self.uart.write(data)
        sleep_ms(10)


# ═══════════════════════════════════════════
#  示例用法
# ═══════════════════════════════════════════

if __name__ == '__main__':
    # 初始化 (根据你的接线调整引脚)
    vm = VoiceModule(uart_id=1, tx=Pin(15), rx=Pin(16))
    
    # 测试播报
    vm.welcome()           # "我在"
    vm.detect_block(1)     # "检测到区块一"
    vm.say_crop('柑橘')    # "当前区块作为柑橘"
    vm.task_done()         # "喷洒完成"
