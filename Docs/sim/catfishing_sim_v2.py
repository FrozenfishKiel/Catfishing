# -*- coding: utf-8 -*-
"""
小猫钓鱼 数值模拟 v0.2  (2026-08-19)  ——  第二轮：按 08-16～08-19 全部拍定口径重算
（v0.1 脚本 catfishing_sim.py 保留不动，供对照首轮报告）

用法：python catfishing_sim_v2.py [a|b|c|d|e|f|all] [old|new]
  old ＝ 报告《模拟报告-2026-08-19》所用口径（拍定前：磨损 0.1／回体 1.5／饵 5／槽 20／巨影 K 2.8／供奉按鱼种列）
  new ＝ 报告建议采纳后的拍定口径（参数页 v0.8，默认）：磨损 0.05／回体 2.5／饵 8／槽 10／巨影 K 5＋逗猫棒竿强度 210／供奉挂体重档
  输出分别存 模拟输出-2026-08-19.txt（old）与 模拟输出-2026-08-19-拍定后.txt（new）

拍定口径来源（SSOT）：
- 鱼表格·第一版（K 列已 ×5，巨影 2.8 不动；重量区间/体力/供奉/窝料属性）
- 参数与校准记录 v0.7 拍定表：白天 20 分钟；竿三档 树枝 25/40/L60 免费 · 玩具 60/70/L80 100金 · 逗猫棒 130/120/L100 150金；
  漂三种 射程 3/5/7 米；可抄距离 1 米；体重档 小<1 / 中1~8 / 大8~15 / 巨影>15；抄网 基础≤8kg / 大≤15kg / 巨影不可抄；
  经验 小1/中3/大8/巨影20、槽 20
- 钓鱼规则（实现规格书）：窝料池 T_base 15/120、K=100、30s×0.9、Total<1 归零；咬钩两步判定（窝料定类→鱼饵定条）；
  3.4 饵：无饵不能抛、进入咬钩即扣 1 份（任何结局）、携带 5 份；2.7 窝料 5 份；
  4.3 判定表从严（向外+拖：①竿强≤min(猫力,鱼力)断竿 → ②鱼力≥猫力拖下水 → ③猫力≥2×鱼力碾压 → ④僵持）；
  4.4 僵持每秒 竿耐久-=鱼力×0.1 / 鱼体力-=猫力×0.08 / 猫体力-=鱼力×0.12；向内+拖 猫体力-=鱼力×0.15；向外+松 +1.5点/秒；
  每钓上一条 -1 耐久；L≥L_max 右键失效只能拖；完美中鱼 普通 力-20%体-15% / 稀有 力-15%体-10%
- 鱼的行为（钱成澔初版＋08-18 扩写，待属主审核——结构已有、数字是快照）：段制切换 发力3~6s/休息2~5s；
  P(向外)=P_base(食肉.7/杂食.5/素食.35)×M_体力(>50%:1/30~50%:.8/<30%:.6)×M_水深(>10m:1/3~10m:1→1.5/<3m:1.8) 夹[.05,.95]；
  垂死挣扎：体力<30% 后每个向外段 35% 触发一次，鱼力×1.5 持续 2 秒；D/L 速率 向内拖-3/向内松-1/向外松+2.5，游速系数 小.8/中1/大1.2/巨影1.5；
  巨影：体力耗尽前 P(向外)=100%、不挣扎
- 装备册 v1.16：保底三件免费（树枝竿/羽毛漂/普通饵）；窝料商店购买；抄网非必带（无网路径＝翻肚拖岸拾取）；
  渔网 roll 5~8 条、稀有不加权、巨影不入网、定价≥期望市价×1.2、30s 死水；「零道具通关」红线用例

假设（文档未定或未填，标 ASSUMPTION，集中放 P 字典便于扫参）：
- 食性（鱼表格挂号列未填）：腥鱼＝食肉、酵鱼＝杂食、香鱼＝素食；稀有＝供奉≥3（稀有度列未填）
- 偏好饵权重（鱼表格 P 列未填）：按饵表「主要目标鱼（示意）」目标 3 : 非目标 1；普通饵默认选虫虫饵
- 抛竿落点＝漂射程（D0=3/5/7 米），忽略精准度偏移；猫站岸边 → 鱼-猫距离 ≈ 鱼-岸距离
- 完美提竿命中 25%；垂死挣扎前摇玩家松线概率 50%（扫 0/1）
- 玩家策略「懂行型」：向内且体力>15 就拖；向外时若能碾压就拖、若能僵持且体力>40 就僵持、否则放线回体；
  （7.1 UI 提示「鱼在发力按住右键放线」的「听话型」永远进不了僵持/碾压——无网时一条鱼都收不了，见报告）
- 搏斗外猫体力自然回复＝1.5点/秒（猫册规则未出，沿用放线快照）；翻肚鱼拖岸不耗猫体力
- 回营补给一趟 90 秒（地图尺度未定）；补饵/补窝料/换竿同趟
- 打窝策略：池 Total<40 且有存货就补一份；鱼价 ASSUMPTION：金币＝max(1, round(kg×10))（商店册价表未出）
- 鱼重在区间内均匀分布
"""
import random, statistics as st, sys, io
from collections import Counter
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')
random.seed(2026)

# ------------------------------------------------------------------ 数据
# name: w=(min,max)kg, K, 体力, 供奉, 窝料属性, 水域, 常规刷新, 食性(ASSUMPTION)
FISH = {
    '河纹鱼':   dict(w=(0.4,3),    K=6,    stam=30,  offer=1,  attr='酵', water='河',  reg=True,  diet='杂食'),
    '小银鱼':   dict(w=(0.05,0.4), K=4,    stam=15,  offer=1,  attr='酵', water='河湖', reg=True,  diet='杂食'),
    '小彩鱼':   dict(w=(0.04,0.4), K=5,    stam=25,  offer=3,  attr='香', water='河湖', reg=True,  diet='素食'),
    '森林长尾鱼':dict(w=(3,8),     K=10,   stam=90,  offer=2,  attr='香', water='河湖', reg=True,  diet='素食'),
    '银月鳟':   dict(w=(3,8),      K=10.5, stam=100, offer=3,  attr='香', water='河湖', reg=False, diet='杂食'),   # 特殊时刻限定
    '湖心巨影': dict(w=(15,40),    K=2.8,  stam=260, offer=10, attr='腥', water='事件', reg=False, diet='食肉'),   # 多人事件鱼
    '花瓣鱼':   dict(w=(0.05,0.4), K=4.5,  stam=20,  offer=3,  attr='香', water='河湖', reg=True,  diet='素食'),
    '风铃鱼':   dict(w=(0.05,0.4), K=4.5,  stam=20,  offer=3,  attr='香', water='河湖', reg=True,  diet='素食'),
    '咸鱼':     dict(w=(0.5,1.2),  K=7.5,  stam=45,  offer=2,  attr='腥', water='河湖', reg=True,  diet='食肉'),
    '臭臭鱼':   dict(w=(0.2,0.4),  K=9,    stam=75,  offer=-1, attr='酵', water='河湖', reg=True,  diet='杂食'),
    '黑鱼':     dict(w=(3,8),      K=11.5, stam=100, offer=2,  attr='腥', water='河',  reg=True,  diet='食肉'),
    '泥鳅':     dict(w=(0.05,0.5), K=3.5,  stam=15,  offer=1,  attr='酵', water='河',  reg=True,  diet='杂食'),
    '河口鲈':   dict(w=(1,5),      K=9,    stam=80,  offer=2,  attr='腥', water='河',  reg=True,  diet='食肉'),
}
RARE = {n for n,v in FISH.items() if v['offer'] >= 3}
BASE_POOL = ['泥鳅','小银鱼','河纹鱼']
CATS = {'普通猫':50, '橘猫':70, '狸花猫':90, '缅因猫':130}
RODS = {'树枝竿':dict(S=25, dur=40,  L=60,  price=0),
        '玩具竿':dict(S=60, dur=70,  L=80,  price=100),
        '逗猫棒竿':dict(S=130,dur=120, L=100, price=150)}
FLOATS = {'羽毛漂':3, '毛线球漂':5, '铃铛漂':7}
NETS = {None:0, '基础抄网':8, '大抄网':15}
GROUNDBAIT = {'虫虫窝':(10,2,1), '花果香窝':(1,10,3), '发酵谷物窝':(0,3,10), '圣光诱鱼窝':(5,8,8)}
BAIT_TARGET = {'虫虫饵':{'河纹鱼','小银鱼','泥鳅'}, '肉块饵':{'黑鱼','湖心巨影'}, '果实饵':{'小彩鱼'},
               '花蜜饵':{'花瓣鱼','风铃鱼','森林长尾鱼'}, '月光饵':{'银月鳟'}}
ATTRS = ['腥','香','酵']

def tier(w):
    return '小' if w < 1 else ('中' if w <= 8 else ('大' if w <= 15 else '巨影'))
SPEED = {'小':0.8,'中':1.0,'大':1.2,'巨影':1.5}
EXP   = {'小':1,'中':3,'大':8,'巨影':20}
def price(w): return max(1, round(w*10))      # ASSUMPTION

P = dict(ROD_WEAR=0.1, FISH_DRAIN=0.08, CAT_STALL=0.12, CAT_PULL=0.15, REGEN=1.5, REGEN_OUT=1.5,
         PERFECT=0.25, RELEASE=0.5, SHORE_VALVE=False, NET_DIST=1.0,
         SEG_OUT=(3,6), SEG_IN=(2,5), P_BASE={'食肉':.7,'杂食':.5,'素食':.35}, CLAMP=(.05,.95),
         STRUG_P=.35, STRUG_MULT=1.5, STRUG_DUR=2, CAP=600, STALL_MIN_STAM=40, PULL_MIN_STAM=15,
         BAIT_W=(3,1), RESTOCK=90, HANDLE=8, T_BASE=(15,120), KPOOL=100, DAY=20*60,
         CARRY=5, EXP_SLOT=20, GIANT_K=2.8, OFFER_TIER=False)

# 口径开关：'old'＝报告（2026-08-19）所用的拍定前口径；'new'＝08-19 报告建议被采纳后的拍定口径（参数页 v0.8）
CANON_NEW = dict(ROD_WEAR=0.05, REGEN=2.5, REGEN_OUT=5.0, CARRY=8, EXP_SLOT=10, GIANT_K=5, OFFER_TIER=True, ROD3_S=210)  # REGEN_OUT=5：2026-08-20 猫册「数值成长」页拍定（搏斗外自然回复）
OFFER_BY_TIER = {'小':1, '中':2, '大':4, '巨影':10}

def apply_canon(mode):
    if mode == 'new':
        P.update(CANON_NEW)
    FISH['湖心巨影']['K'] = P['GIANT_K']
    if 'ROD3_S' in P: RODS['逗猫棒竿']['S'] = P['ROD3_S']      # 巨影 K=5 → 鱼力上限 200，逗猫棒竿强度须 >200
    print(f'口径＝{"08-19 拍定后（竿磨损0.05／回体2.5／饵8份／经验槽10／巨影K5＋逗猫棒竿强度210／供奉挂体重档）" if mode=="new" else "报告口径（08-19 拍定前）"}')

def offer_of(name, w, p=P):
    if p['OFFER_TIER']:
        return -1 if name == '臭臭鱼' else OFFER_BY_TIER[tier(w)]
    return FISH[name]['offer']

def m_depth(D):
    if D > 10: return 1.0
    if D >= 3: return 1.0 + 0.5*(10-D)/7
    return 1.8

def m_stam(r):
    return 1.0 if r > .5 else (.8 if r >= .3 else .6)

# ------------------------------------------------------------------ 搏斗
def fight(C, rod, dur_left, D0, name, w, net_cap=0, n_cats=1, perfect=False, cat_stam=100.0, p=P):
    """一次搏斗。返回 dict(res, t, dmg, stall, cat_stam, netted, clean)
    res ∈ landed / dragged / rodbreak / escape ；clean＝靠抄网收鱼且全程零僵持（「白捡」）"""
    f = FISH[name]; F0 = w*f['K']; stam0 = f['stam']
    if perfect:
        if name in RARE: F0 *= .85; stam0 *= .90
        else:            F0 *= .80; stam0 *= .85
    S, Lmax = rod['S'], rod['L']
    F, stam = F0, stam0
    spd = SPEED[tier(w)]
    giant = (name == '湖心巨影')
    D = L = float(D0); t = 0; dmg = 0.0; stall = 0
    outward = True; seg = random.uniform(*p['SEG_OUT'])
    struggled = False; strug = 0; will_release = False
    valve_floor = 0.0
    if p['SHORE_VALVE'] == 'mid' and tier(w) in ('中','大','巨影'): valve_floor = 1.5
    elif p['SHORE_VALVE'] is True and tier(w) in ('大','巨影'): valve_floor = 1.5
    while t < p['CAP']:
        t += 1
        if seg <= 0:
            if giant: p_out = 1.0
            else:
                p_out = p['P_BASE'][f['diet']] * m_stam(stam/stam0) * m_depth(D)
                p_out = min(p['CLAMP'][1], max(p['CLAMP'][0], p_out))
            outward = random.random() < p_out
            seg = random.uniform(*(p['SEG_OUT'] if outward else p['SEG_IN']))
            if outward and not giant and not struggled and stam/stam0 < .3 and random.random() < p['STRUG_P']:
                struggled = True; strug = p['STRUG_DUR']; will_release = random.random() < p['RELEASE']
        seg -= 1
        Fnow = F * (p['STRUG_MULT'] if strug > 0 else 1.0)
        in_strug = strug > 0
        if strug > 0: strug -= 1
        if not outward:                                   # 鱼向内游（休息）
            if cat_stam > p['PULL_MIN_STAM']:
                D -= 3*spd; L = max(D, L - 3*spd); cat_stam -= p['CAT_PULL']*Fnow/n_cats
                if cat_stam <= 0: return dict(res='dragged', t=t, dmg=dmg, stall=stall, cat_stam=0, netted=False, clean=False)
            else:
                D -= 1*spd
            D = max(D, valve_floor, 0.0)
        else:                                             # 鱼向外游（发力）
            forced = L >= Lmax
            if forced: pull = True
            elif p.get('OBEDIENT'): pull = False                        # 听话型：UI 说「鱼在发力按住右键放线」就只放
            elif in_strug and will_release: pull = False
            elif S <= min(C, Fnow) or Fnow >= C: pull = False          # 懂行：拖必输，只放
            elif C >= 2*Fnow: pull = True                               # 碾压
            else:
                # 看条型玩家：三条资源条都可见（4.2），投影谁先归零；投影能赢就死扛，否则体力>40 才磨（为抄网/翻肚攒鱼疲劳）
                t_fish = stam/(p['FISH_DRAIN']*C); t_cat = cat_stam/(p['CAT_STALL']*Fnow/n_cats); t_rod = (dur_left-dmg)/(p['ROD_WEAR']*Fnow)
                if t_fish <= min(t_cat, t_rod): pull = True
                else: pull = cat_stam > p['STALL_MIN_STAM'] and t_rod > 1.5
            if pull:
                if S <= min(C, Fnow): return dict(res='rodbreak', t=t, dmg=dur_left, stall=stall, cat_stam=cat_stam, netted=False, clean=False)
                if Fnow >= C:         return dict(res='dragged', t=t, dmg=dmg, stall=stall, cat_stam=cat_stam, netted=False, clean=False)
                if C >= 2*Fnow:       return dict(res='landed', t=t, dmg=dmg, stall=stall, cat_stam=cat_stam, netted=False, clean=False, how='碾压')
                stall += 1; dmg += p['ROD_WEAR']*Fnow; stam -= p['FISH_DRAIN']*C; cat_stam -= p['CAT_STALL']*Fnow/n_cats
                if stam <= 0:
                    tdrag = int(D/(3*spd)) + 1                           # 翻肚→拖上岸拾取（无网路径）
                    return dict(res='landed', t=t+tdrag, dmg=dmg, stall=stall, cat_stam=cat_stam, netted=False, clean=False, how='翻肚')
                if cat_stam <= 0:     return dict(res='dragged', t=t, dmg=dmg, stall=stall, cat_stam=0, netted=False, clean=False)
                if dur_left - dmg <= 0: return dict(res='rodbreak', t=t, dmg=dur_left, stall=stall, cat_stam=cat_stam, netted=False, clean=False)
            else:
                D += 2.5*spd; L += 2.5*spd; cat_stam = min(100.0, cat_stam + p['REGEN'])
        if net_cap and w <= net_cap and D < p['NET_DIST']:
            return dict(res='landed', t=t, dmg=dmg, stall=stall, cat_stam=cat_stam, netted=True, clean=(stall == 0), how='抄网')
    return dict(res='escape', t=t, dmg=dmg, stall=stall, cat_stam=cat_stam, netted=False, clean=False)

# ------------------------------------------------------------------ 模型A：搏斗矩阵
def model_a():
    print('='*110)
    print('模型A｜搏斗结局矩阵（K×5 后；鱼按最大体重；羽毛漂 D0=3m；N=300；普通提竿；懂行型策略）')
    print('格式：胜率% / 主要失败态 / 均时s / 竿耗/条 ；「碾压」＝全胜且秒上岸 ；括号内＝有基础抄网(≤8kg)或大抄网(≤15kg)时胜率与白捡率')
    print('='*110)
    combos = [('普通猫',1),('橘猫',1),('狸花猫',1),('缅因猫',1),('普通猫',2),('缅因猫',2),('缅因猫',3)]
    targets = ['河纹鱼','咸鱼','河口鲈','森林长尾鱼','黑鱼','湖心巨影']
    for rname, rod in RODS.items():
        print(f'\n--- {rname}（强度{rod["S"]} 耐久{rod["dur"]} L_max{rod["L"]}） ---')
        print(f'{"阵容":<8}' + ''.join(f'{t:<24}' for t in targets))
        for cat, n in combos:
            C = CATS[cat]*n; label = f'{cat}x{n}'
            row = f'{label:<8}'
            for name in targets:
                wmax = FISH[name]['w'][1]
                cell = cell_for(C, rod, name, wmax, n)
                row += f'{cell:<24}'
            print(row)
    print('\n模型A附0｜湖心巨影按体重（K=' + str(FISH['湖心巨影']['K']) + '；逗猫棒竿强度' + str(RODS['逗猫棒竿']['S']) + '）：15/25/40 kg 的鱼力与各阵容结局（N=200）')
    for wg in (15, 25, 40):
        F = wg*FISH['湖心巨影']['K']
        row = f'  巨影{wg}kg 鱼力{F:.0f}: '
        for cat, n in [('狸花猫',1),('缅因猫',1),('普通猫',2),('普通猫',3),('缅因猫',2),('缅因猫',3)]:
            row += f'{cat}x{n}={cell_for(CATS[cat]*n, RODS["逗猫棒竿"], "湖心巨影", wg, n, N=200):<20}'
        print(row)
    ratio = 260*P['ROD_WEAR']/(120*P['FISH_DRAIN'])
    print(f'  注：僵持要竿撑到鱼倒需 C>{ratio:.2f}F（竿120耐久/磨损{P["ROD_WEAR"]}/鱼体力260），碾压在 C≥2F 触发——窗口{"为空：巨影不可能被「僵持遛翻」，只能碾压" if ratio>=2 else "＝("+format(ratio,".2f")+"F, 2F)，合力≥3 猫摊体力才进得去"}')
    print('\n模型A附1｜纯僵持竞速（连续僵持：谁先归零；竿耐久满额；不含距离战）')
    print(f'{"对局":<28}{"鱼力":>6}{"鱼倒s":>7}{"猫脱力s":>8}{"竿断s":>7}  先发生 ｜ 遛翻所需竿耐久')
    for name in ['河口鲈','森林长尾鱼','黑鱼','湖心巨影']:
        f = FISH[name]; F = f['w'][1]*f['K']
        for cat, n, rname in [('普通猫',1,'玩具竿'),('狸花猫',1,'逗猫棒竿'),('缅因猫',1,'逗猫棒竿'),('缅因猫',2,'逗猫棒竿'),('缅因猫',3,'逗猫棒竿')]:
            C = CATS[cat]*n; rod = RODS[rname]
            if rod['S'] <= min(C,F) or F >= C:
                print(f'{name+" vs "+cat+"x"+str(n)+"/"+rname:<28}{F:>6.0f}   —  进不了僵持（{"竿断" if rod["S"]<=min(C,F) else "拖下水"}）'); continue
            t_fish = f['stam']/(P['FISH_DRAIN']*C); t_cat = 100/(P['CAT_STALL']*F/n); t_rod = rod['dur']/(P['ROD_WEAR']*F)
            first = min(('鱼倒',t_fish),('猫脱力',t_cat),('竿断',t_rod), key=lambda x:x[1])[0]
            need = t_fish*P['ROD_WEAR']*F
            print(f'{name+" vs "+cat+"x"+str(n)+"/"+rname:<28}{F:>6.0f}{t_fish:>7.1f}{t_cat:>8.1f}{t_rod:>7.1f}  {first:<6}｜ {need:.0f}（竿上限{rod["dur"]}）')
    print('\n模型A附2｜树枝竿可拖鱼力上限＝25：各鱼种超过它的最小体重（超过即「向外拖＝瞬断」，只能放线或靠抄网）')
    for name, f in FISH.items():
        wcut = 25/f['K']; lo,hi = f['w']
        if wcut < lo: s = f'全区间超限（{lo}kg 起鱼力{lo*f["K"]:.0f}）'
        elif wcut > hi: s = '全区间可拖'
        else: s = f'>{wcut:.2f}kg 超限（占区间 {(hi-wcut)/(hi-lo)*100:.0f}%）'
        print(f'  {name:<8} K={f["K"]:<5} {s}')

def ladder():
    print('\n模型A附3｜分层一览（解析，不跑蒙特卡洛）：鱼按区间中位/最大体重；单猫；逗猫棒竿（强130/耐120）；'
          '碾压=C≥2F ｜ 胜=僵持投影鱼先倒 ｜ 险=能僵持但猫先脱力或竿先断 ｜ 拖不动=F≥C ｜ 树枝竿列：×=向外拖即瞬断(F≥25)')
    print(f'{"鱼@体重":<14}{"鱼力":>5}  ' + ''.join(f'{c:<10}' for c in CATS) + ' 树枝竿')
    for name, f in FISH.items():
        if name == '湖心巨影': ws = [15, 25, 40]
        else: ws = [round((f['w'][0]+f['w'][1])/2, 2), f['w'][1]]
        for w in ws:
            F = w*f['K']; row = f'{name+"@"+str(w):<14}{F:>5.0f}  '
            for cat, C in CATS.items():
                if F >= C: z = '拖不动'
                elif C >= 2*F: z = '碾压'
                else:
                    t_fish = f['stam']/(P['FISH_DRAIN']*C); t_cat = 100/(P['CAT_STALL']*F); t_rod = 120/(P['ROD_WEAR']*F)
                    z = '胜' if t_fish <= min(t_cat, t_rod) else ('险·脱力' if t_cat < t_rod else '险·竿断')
                row += f'{z:<10}'
            row += '  ×' if F >= 25 else '  可'
            print(row)

def obedient():
    print('\n模型A附4｜「听话型」玩家（照 7.1 提示：鱼发力只放线、鱼累了才拖；从不在向外段拖）vs「懂行型」（向外段敢拖→碾压/僵持）')
    print('普通猫＋树枝竿＋羽毛漂；N=300；格式 胜率/均时s；听话型的胜只来自线放尽被迫拖时恰好碾压、或抄网')
    print(f'{"鱼":<14}{"懂行·无网":<16}{"懂行·基础网":<16}{"听话·无网":<16}{"听话·基础网":<16}')
    for name, w in [('小银鱼',0.2),('河纹鱼',2.0),('咸鱼',1.0),('河口鲈',2.0),('河口鲈',4.0),('森林长尾鱼',5.0)]:
        row = f'{name+"@"+str(w):<14}'
        for ob in (False, True):
            for cap in (0, 8):
                p = dict(P); p['OBEDIENT'] = ob
                res = Counter(); ts = []
                for _ in range(300):
                    r = fight(50, RODS['树枝竿'], 40, 3, name, w, net_cap=cap, p=p)
                    res[r['res']] += 1; ts.append(r['t'])
                row += f'{res["landed"]/3:.0f}%/{st.mean(ts):.0f}s{"":<8}'
        print(row)

def cell_for(C, rod, name, w, n, N=300):
    res = Counter(); times = []; dmgs = []
    for _ in range(N):
        r = fight(C, rod, rod['dur'], 3, name, w, net_cap=0, n_cats=n)
        res[r['res']] += 1; times.append(r['t']); dmgs.append(r['dmg'] + (1 if r['res']=='landed' else 0))
    land = res['landed']/N*100
    if land == 100 and st.mean(times) <= 2: base = '碾压'
    else:
        worst = max((k for k in res if k != 'landed'), key=lambda k: res[k], default='')
        wtxt = {'dragged':'拖走','rodbreak':'断竿','escape':'脱钩'}.get(worst,'')
        base = f'{land:.0f}%{("/"+wtxt) if worst else ""}/{st.mean(times):.0f}s/竿{st.mean(dmgs):.0f}'
    # 抄网列
    extra = ''
    for netn, cap in (('基础抄网',8),('大抄网',15)):
        if w > cap or name == '湖心巨影': continue
        if base == '碾压': continue
        res2 = Counter(); clean = 0
        for _ in range(N):
            r = fight(C, rod, rod['dur'], 3, name, w, net_cap=cap, n_cats=n)
            res2[r['res']] += 1; clean += r['clean']
        extra = f'({"基" if cap==8 else "大"}网{res2["landed"]/N*100:.0f}%白捡{clean/N*100:.0f}%)'
        break
    return base + extra

# ------------------------------------------------------------------ 模型B：一日渔获（饵约束＋两步选鱼＋抄网＋回营）
def interval(total, p=P):
    return (p['T_BASE'][0] if total > 0 else p['T_BASE'][1]) / (1 + total/p['KPOOL'])

def pick_fish(pool, bait, p=P):
    total = sum(pool)
    if total < 1:
        cands = BASE_POOL
    else:
        r = random.random()*total
        attr = '腥' if r < pool[0] else ('香' if r < pool[0]+pool[1] else '酵')
        cands = [n for n,v in FISH.items() if v['attr'] == attr and v['reg'] and '河' in v['water']]
        if not cands: cands = BASE_POOL
    wt = [p['BAIT_W'][0] if n in BAIT_TARGET.get(bait, ()) else p['BAIT_W'][1] for n in cands]
    return random.choices(cands, weights=wt)[0]

def day_sim(n_players=1, cat='普通猫', rod='树枝竿', flt='羽毛漂', net=None, bait='虫虫饵', gb='发酵谷物窝',
            runs=100, p=P, buy_same_rod=True, carry=None):
    if carry is None: carry = p['CARRY']
    R = RODS[rod]; C = CATS[cat]; D0 = FLOATS[flt]; cap = NETS[net]
    agg = Counter(); out = Counter(); rows = []
    for _ in range(runs):
        pool = [0.0,0.0,0.0]; decay_t = 0
        pls = [dict(state='idle', timer=0, dur=R['dur'], bait=carry, gb=carry if gb else 0, stam=100.0,
                    catch=Counter(), coins=0, offer=0, exp=0, baits=0, gbs=0, trips=0, breaks=0, res=Counter(), fight_s=0, spent=0, stam_in=[])
               for _ in range(n_players)]
        for t in range(p['DAY']):
            decay_t += 1
            if decay_t >= 30:
                decay_t = 0; pool = [x*0.9 for x in pool]
                if sum(pool) < 1: pool = [0.0,0.0,0.0]
            for pl in pls:
                if pl['state'] in ('idle','waiting','restock'):
                    pl['stam'] = min(100.0, pl['stam'] + p['REGEN_OUT'])    # ASSUMPTION 搏斗外自然回复
                if pl['timer'] > 0:
                    pl['timer'] -= 1; continue
                if pl['state'] == 'restock':
                    pl['bait'] = carry; pl['gb'] = carry if gb else 0; pl['trips'] += 1
                    if pl['dur'] <= 0:
                        pl['dur'] = R['dur']
                        if buy_same_rod: pl['spent'] += R['price']
                    pl['state'] = 'idle'
                if pl['state'] == 'idle':
                    if pl['bait'] == 0 or pl['dur'] <= 0:
                        pl['state'] = 'restock'; pl['timer'] = p['RESTOCK']; continue
                    if gb and sum(pool) < 40 and pl['gb'] > 0:
                        v = GROUNDBAIT[gb]; pool = [pool[i]+v[i] for i in range(3)]
                        pl['gb'] -= 1; pl['gbs'] += 1; pl['timer'] = 5; continue
                    pl['state'] = 'waiting'; pl['timer'] = max(1, int(round(interval(sum(pool), p))))
                elif pl['state'] == 'waiting':
                    name = pick_fish(pool, bait, p)
                    f = FISH[name]; w = random.uniform(*f['w'])
                    perfect = random.random() < p['PERFECT']
                    pl['stam_in'].append(pl['stam'])
                    r = fight(C, R, pl['dur'], D0, name, w, net_cap=cap, n_cats=1, perfect=perfect, cat_stam=pl['stam'], p=p)
                    pl['bait'] -= 1; pl['baits'] += 1
                    pl['dur'] -= r['dmg']; pl['stam'] = max(0.0, r['cat_stam']); pl['fight_s'] += r['t']
                    pl['res'][r['res']] += 1
                    pl['timer'] = int(r['t']) + p['HANDLE']
                    if r['res'] == 'landed':
                        pl['dur'] -= 1
                        pl['catch'][name] += 1; pl['coins'] += price(w); pl['offer'] += offer_of(name, w, p); pl['exp'] += EXP[tier(w)]
                    if r['res'] == 'rodbreak' or pl['dur'] <= 0:
                        pl['dur'] = 0; pl['breaks'] += 1
                    pl['state'] = 'idle'
        for pl in pls:
            agg += pl['catch']; out += pl['res']
            rows.append(pl)
    m = lambda k: st.mean(pl[k] if not isinstance(pl[k], Counter) else sum(pl[k].values()) for pl in rows)
    return dict(catch=m('catch'), offer=m('offer'), exp=m('exp'), coins=m('coins'), baits=m('baits'), gbs=m('gbs'),
                trips=m('trips'), breaks=m('breaks'), spent=m('spent'), fight_s=m('fight_s'),
                stam_in=st.mean(x for pl in rows for x in pl['stam_in']) if any(pl['stam_in'] for pl in rows) else 100,
                res=out, top=agg.most_common(6), n=len(rows))

def p_day(): return P['DAY']

def model_b():
    print('\n'+'='*110)
    print('模型B｜一日渔获（白天 20 分钟；每人均值；饵 5 份/趟·窝料 5 份/趟·回营 90s；咬钩即扣饵；两步选鱼；N=100 天）')
    print('='*110)
    hdr = f'{"场景":<40}{"渔获":>5}{"供奉":>5}{"经验":>5}{"金币":>6}{"饵":>5}{"窝料":>5}{"回营":>5}{"断竿":>5}  结局(上鱼/拖走/断竿/脱钩)'
    print(hdr)
    S = [
        dict(label='①免费三件·无窝料（零花费基线）',   gb=None),
        dict(label='②免费三件＋酵窝（零道具基线）',     gb='发酵谷物窝'),
        dict(label='③免费三件＋香窝（花蜜饵）',         gb='花果香窝', bait='花蜜饵'),
        dict(label='④免费三件＋腥窝（肉块饵）',         gb='虫虫窝', bait='肉块饵'),
        dict(label='⑤②＋基础抄网',                     gb='发酵谷物窝', net='基础抄网'),
        dict(label='⑥玩具竿＋基础网＋腥窝（普通猫）',   gb='虫虫窝', bait='肉块饵', rod='玩具竿', net='基础抄网'),
        dict(label='⑦逗猫棒竿＋大网＋腥窝（普通猫）',   gb='虫虫窝', bait='肉块饵', rod='逗猫棒竿', net='大抄网'),
        dict(label='⑧逗猫棒竿＋大网＋腥窝（缅因猫）',   gb='虫虫窝', bait='肉块饵', rod='逗猫棒竿', net='大抄网', cat='缅因猫'),
        dict(label='⑨逗猫棒竿＋无网＋腥窝（缅因猫）',   gb='虫虫窝', bait='肉块饵', rod='逗猫棒竿', net=None, cat='缅因猫'),
        dict(label='⑩4人共窝·免费三件＋酵窝',          gb='发酵谷物窝', n=4),
        dict(label='⑪8人共窝·免费三件＋酵窝',          gb='发酵谷物窝', n=8),
        dict(label='⑫③＋铃铛漂 7m（落点远）',          gb='花果香窝', bait='花蜜饵', flt='铃铛漂'),
    ]
    results = {}
    for s in S:
        r = day_sim(n_players=s.get('n',1), cat=s.get('cat','普通猫'), rod=s.get('rod','树枝竿'), flt=s.get('flt','羽毛漂'),
                    net=s.get('net'), bait=s.get('bait','虫虫饵'), gb=s.get('gb'))
        results[s['label']] = r
        tot = sum(r['res'].values()) or 1
        rs = '/'.join(f'{r["res"][k]/tot*100:.0f}%' for k in ('landed','dragged','rodbreak','escape'))
        print(f'{s["label"]:<40}{r["catch"]:>5.1f}{r["offer"]:>5.1f}{r["exp"]:>5.1f}{r["coins"]:>6.0f}{r["baits"]:>5.1f}{r["gbs"]:>5.1f}{r["trips"]:>5.1f}{r["breaks"]:>5.2f}  {rs}  开战均体力{r["stam_in"]:.0f} 搏斗占时{r["fight_s"]/p_day()*100:.0f}%')
        if s['label'][0] in '①②③④⑥⑦⑧':
            print(f'{"":<6}└ 鱼种TOP: ' + '，'.join(f'{k}{v/r["n"]:.1f}' for k,v in r['top']))
    print('\n模型B附｜饵约束敏感性（场景②零道具基线）：回营单程耗时 × 饵携带上限 → 渔获/人/天（括号＝回营趟数）')
    print(f'{"饵携带":<8}' + ''.join(f'{"回营"+str(rs)+"s":<16}' for rs in (30, 90, 150)))
    for carry in (5, 10):
        row = f'{carry:<8}'
        for rs in (30, 90, 150):
            p = dict(P); p['RESTOCK'] = rs
            r = day_sim(gb='发酵谷物窝', p=p, carry=carry, runs=60)
            row += f'{r["catch"]:.1f}（{r["trips"]:.1f}趟）{"":<4}'
        print(row)
    return results

# ------------------------------------------------------------------ 模型C：额度 / 世界进度 / 经验节奏
def model_c(B):
    print('\n'+'='*110)
    print('模型C｜供奉·经验·进度节奏（用模型B吞吐反推；献祭为夜晚仪式、白天只攒）')
    print('='*110)
    print('※ 守护指标（2026-08-21 四镜头评审挂号）：三选一触发率——吃倾向路线须 ≥1.5 次/人/白天。')
    print('  它是成长系统的节奏地基，却由别册的经济参数（额度曲线／售价／道具定价）间接决定；')
    print('  其他册调「卖」「献」的边际收益时须回跑本节，跌破 1.5 即为回归。')
    pick = {k:v for k,v in B.items()}
    for label, r in pick.items():
        if label[0] in '②③④⑤⑩':
            print(f'{label:<40} 供奉/人/天≈{r["offer"]:.0f}  经验/人/天≈{r["exp"]:.0f}→三选一≈{r["exp"]/P["EXP_SLOT"]:.1f}次(全吃)/{r["exp"]/P["EXP_SLOT"]/2:.1f}次(吃一半)  金币/人/天≈{r["coins"]:.0f}')
    r2 = pick['②免费三件＋酵窝（零道具基线）']; r3 = pick['③免费三件＋香窝（花蜜饵）']
    best = ('香窝', r3) if r3['offer'] > r2['offer'] else ('酵窝', r2)
    note = '（供奉已挂体重档 小1/中2/大4/巨影10，香系小鱼不再占优）' if P['OFFER_TIER'] else '（倒挂未改：香系小鱼供3、8kg大鱼供2）'
    print(f'\n免费装下献祭最优＝{best[0]}：香窝 {r3["offer"]:.0f} vs 酵窝 {r2["offer"]:.0f} 供奉/人/天' + note)
    for n in (1,4,8):
        team = best[1]['offer']*n
        print(f'  {n}人队·{best[0]}全献：全队供奉/天≈{team:.0f} → 10 天毕业的世界进度总量 ≈ {team*10*0.7:.0f}（×0.7 冗余）')
    print('  额度参考（数量口径）：若首日额度＝单人基线渔获的 30%～40%、逐日爬升到第 10 天 ≈ 80%，则零道具基线可通关，见报告建议')

# ------------------------------------------------------------------ 模型D：敏感性
def model_d():
    print('\n'+'='*110)
    print('模型D｜敏感性：竿磨损系数 / 放线回体 / 挣扎松线率 / 贴岸阀 / 漂射程（N=400，普通提竿）')
    print('='*110)
    cases = [('普通猫/玩具竿 vs 河口鲈5kg', 50, '玩具竿', '河口鲈', 5, 1, 0),
             ('狸花猫/逗猫棒 vs 黑鱼8kg',  90, '逗猫棒竿', '黑鱼', 8, 1, 0),
             ('缅因猫/逗猫棒 vs 黑鱼8kg',  130,'逗猫棒竿', '黑鱼', 8, 1, 0),
             ('3缅因/逗猫棒 vs 巨影40kg',  390,'逗猫棒竿', '湖心巨影', 40, 3, 0),
             ('普通猫/树枝竿+基础网 vs 长尾8kg', 50, '树枝竿', '森林长尾鱼', 8, 1, 8),
             ('普通猫/玩具竿+基础网 vs 河口鲈5kg', 50, '玩具竿', '河口鲈', 5, 1, 8),
             ('狸花猫/逗猫棒+大网 vs 黑鱼8kg', 90, '逗猫棒竿', '黑鱼', 8, 1, 15)]
    variants = [('基准', {}), ('竿磨损0.07', {'ROD_WEAR':.07}), ('回体4/s', {'REGEN':4.0}), ('挣扎必松', {'RELEASE':1.0}),
                ('挣扎不松', {'RELEASE':0.0}), ('贴岸阀·大档', {'SHORE_VALVE':True}), ('贴岸阀·中档起', {'SHORE_VALVE':'mid'}), ('D0=7m铃铛漂', {'D0':7})]
    print(f'{"对局":<34}' + ''.join(f'{v[0]:<18}' for v in variants))
    for label, C, rname, name, w, n, cap in cases:
        row = f'{label:<34}'
        for vlabel, over in variants:
            p = dict(P); p.update({k:v for k,v in over.items() if k != 'D0'}); D0 = over.get('D0', 3)
            res = Counter(); clean = 0; N = 400
            for _ in range(N):
                r = fight(C, RODS[rname], RODS[rname]['dur'], D0, name, w, net_cap=cap, n_cats=n, p=p)
                res[r['res']] += 1; clean += r['clean']
            s = f'{res["landed"]/N*100:.0f}%'
            worst = max((k for k in res if k != 'landed'), key=lambda k: res[k], default='')
            if worst: s += {'dragged':'拖','rodbreak':'断','escape':'脱'}[worst]
            if cap: s += f'(白捡{clean/N*100:.0f}%)'
            row += f'{s:<18}'
        print(row)

# ------------------------------------------------------------------ 模型E：渔网期望
def model_e():
    print('\n'+'='*110)
    print('模型E｜渔网期望产出（roll 5~8 条＝均 6.5；按窝点占比定属性、属性内均匀、稀有不加权、巨影不入网、无饵无搏斗）')
    print('='*110)
    for gbn, v in GROUNDBAIT.items():
        tot = sum(v); ev = eo = ee = 0.0
        for i, a in enumerate(ATTRS):
            cands = [n for n, f in FISH.items() if f['attr'] == a and f['reg'] and '河' in f['water']]
            if not cands: continue
            for n in cands:
                f = FISH[n]; wm = sum(f['w'])/2
                pr = v[i]/tot/len(cands)
                ev += pr*price(wm); eo += pr*offer_of(n, wm); ee += pr*EXP[tier(wm)]
        print(f'  {gbn:<7} 池{v}: 期望 金币{ev*6.5:.0f} / 供奉{eo*6.5:.1f} / 经验{ee*6.5:.1f}（每网 6.5 条） → 定价下限≈{ev*6.5*1.2:.0f} 金币；'
              f'死水 30s 损失≈{30/interval(53):.0f} 次咬钩')

# ------------------------------------------------------------------ 模型F：建议参数复跑（竿磨损 0.05 / 放线回体 2.5）
SUGGEST = dict(ROD_WEAR=0.05, REGEN=2.5)

def model_f():
    print('\n'+'='*110)
    print('模型F｜建议参数复跑（僵持竿磨损 0.1→0.05、放线回体 1.5→2.5 点/秒；其余不变）——搏斗矩阵关键格 ＋ 一日渔获')
    print('='*110)
    p = dict(P); p.update(SUGGEST)
    print('关键对局（N=400，普通提竿／完美）：')
    for label, C, rname, name, w, n in [('普通猫/玩具竿 vs 河口鲈5kg',50,'玩具竿','河口鲈',5,1),('橘猫/逗猫棒 vs 长尾5.5kg',70,'逗猫棒竿','森林长尾鱼',5.5,1),
                                         ('狸花猫/逗猫棒 vs 长尾8kg',90,'逗猫棒竿','森林长尾鱼',8,1),('狸花猫/逗猫棒 vs 黑鱼5.5kg',90,'逗猫棒竿','黑鱼',5.5,1),
                                         ('缅因猫/逗猫棒 vs 黑鱼8kg',130,'逗猫棒竿','黑鱼',8,1),('缅因猫/逗猫棒 vs 巨影25kg',130,'逗猫棒竿','湖心巨影',25,1),
                                         ('缅因猫/逗猫棒 vs 巨影40kg',130,'逗猫棒竿','湖心巨影',40,1),('2普通/逗猫棒 vs 黑鱼8kg',100,'逗猫棒竿','黑鱼',8,2),
                                         ('2普通/逗猫棒 vs 巨影25kg',100,'逗猫棒竿','湖心巨影',25,2),('3普通/逗猫棒 vs 巨影40kg',150,'逗猫棒竿','湖心巨影',40,3),
                                         ('缅因+狸花/逗猫棒 vs 巨影40kg',220,'逗猫棒竿','湖心巨影',40,2)]:
        out = []
        for perfect in (False, True):
            res = Counter(); ts = []; dm = []
            for _ in range(400):
                r = fight(C, RODS[rname], RODS[rname]['dur'], 3, name, w, n_cats=n, perfect=perfect, p=p)
                res[r['res']] += 1; ts.append(r['t']); dm.append(r['dmg'])
            out.append(f'{res["landed"]/4:.0f}%/{st.mean(ts):.0f}s/竿{st.mean(dm):.0f}')
        print(f'  {label:<30} 普通提竿 {out[0]:<20} 完美 {out[1]}')
    print('\n一日渔获（同模型B列）：')
    for label, kw in [('②免费三件＋酵窝', dict(gb='发酵谷物窝')), ('③免费三件＋香窝', dict(gb='花果香窝', bait='花蜜饵')),
                      ('⑥玩具竿＋基础网＋腥窝（普通猫）', dict(gb='虫虫窝', bait='肉块饵', rod='玩具竿', net='基础抄网')),
                      ('⑦逗猫棒竿＋大网＋腥窝（普通猫）', dict(gb='虫虫窝', bait='肉块饵', rod='逗猫棒竿', net='大抄网')),
                      ('⑧逗猫棒竿＋大网＋腥窝（缅因猫）', dict(gb='虫虫窝', bait='肉块饵', rod='逗猫棒竿', net='大抄网', cat='缅因猫'))]:
        r = day_sim(p=p, **kw)
        tot = sum(r['res'].values()) or 1
        rs = '/'.join(f'{r["res"][k]/tot*100:.0f}%' for k in ('landed','dragged','rodbreak','escape'))
        print(f'  {label:<34}{r["catch"]:>5.1f}{r["offer"]:>5.1f}{r["exp"]:>5.1f}{r["coins"]:>6.0f}{r["baits"]:>5.1f}{r["gbs"]:>5.1f}{r["trips"]:>5.1f}{r["breaks"]:>5.2f}  {rs}')

if __name__ == '__main__':
    which = sys.argv[1] if len(sys.argv) > 1 else 'all'
    apply_canon(sys.argv[2] if len(sys.argv) > 2 else 'new')
    if which in ('a','all'): model_a(); ladder(); obedient()
    if which in ('b','all','c'):
        B = model_b()
        model_c(B)
    if which in ('d','all'): model_d()
    if which in ('e','all'): model_e()
    if which in ('f','all'): model_f()
