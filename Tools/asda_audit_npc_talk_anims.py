# Copyright (c) 2026 @sitxovski (Discord)
# asda_audit_npc_talk_anims.py — аудит клипов NPC (idle/разговор) по ИСХОДНЫМ .jbb оригинала (NpcMotion.baz).
#   Задача 04.11.2026 (npc_talk_anim, баг владельца «анимация NPC Ian ломается при начале разговора»).
#   Для каждой модели NPC: набор клипов, скелет (число/имена костей по клипам), NaN, скачки масштаба,
#   рывки между соседними кейфреймами, шов петли и стыки переходов цепочки разговора
#   (idle → клип 2 → клип 3 (петля) → клип 4 → idle; таблица — AAsdaNpcActor::SetEntityAnimState).
#   Меряется мировая позиция костей (после перевода локальных ключей, как в оригинале) — макс. по костям.
#   Запуск (вне UE):  python asda_audit_npc_talk_anims.py [out.json]
import sys, os, re, json, zipfile
import numpy as np


def _clip_world(jbb):
    """Мировые позиции костей клипа по кадрам: массив [K, B, 3] (короткие дорожки держат последний ключ)."""
    bones = jbb['bones']
    K = max((b['keyCount'] for b in bones), default=0)
    if K == 0:
        return np.zeros((0, len(bones), 3)), [b['name'] for b in bones]
    P = np.zeros((K, len(bones), 3))
    for j, b in enumerate(bones):
        kc = b['keyCount']
        if kc == 0:
            P[:, j, :] = np.nan
            continue
        idx = np.minimum(np.arange(K), kc - 1)
        P[:, j, :] = b['pos'][idx]
    return P, [b['name'] for b in bones]


def _scale_spikes(jbb):
    """Макс. отклонение масштаба кости от 1 и макс. скачок масштаба между соседними ключами."""
    dev = jump = 0.0
    for b in jbb['bones']:
        if b['keyCount'] == 0:
            continue
        s = b['scale'].astype(np.float64)
        if not np.all(np.isfinite(s)):
            return float('inf'), float('inf')
        dev = max(dev, float(np.max(np.abs(s - 1.0))))
        if len(s) > 1:
            jump = max(jump, float(np.max(np.abs(np.diff(s, axis=0)))))
    return dev, jump


def _pose_dist(a, b, mask=None):
    """Макс. по костям расстояние между двумя позами [B,3] (кости одного скелета; разные — по общему префиксу)."""
    n = min(len(a), len(b))
    d = np.linalg.norm(a[:n] - b[:n], axis=-1)
    d = d[np.isfinite(d)]
    return float(d.max()) if d.size else 0.0


def main():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import asda_paths as P
    import asda_skinned_to_gltf as S
    out_path = sys.argv[1] if len(sys.argv) > 1 else None
    za = zipfile.ZipFile(os.path.join(P.DATA, 'NPC', 'Motion', 'NpcMotion.baz'))
    clips = {}
    for n in za.namelist():
        m = re.match(r'(.+)_(\d+)\.jbb$', n, re.I)
        if m:
            clips.setdefault(m.group(1).lower(), {})[int(m.group(2))] = n
    res = []
    for model in sorted(clips):
        rec = dict(model=model, clips=sorted(clips[model]), issues=[], clip=[], trans={})
        world = {}
        names0 = None
        height = 1.0
        for N in sorted(clips[model]):
            jbb = S.parse_jbb(za.read(clips[model][N]))
            if not jbb:
                rec['issues'].append('clip %d: не читается' % N)
                continue
            Pw, names = _clip_world(jbb)
            if names0 is None:
                names0 = names
                fin = Pw[np.isfinite(Pw).all(-1)]
                height = float(np.ptp(fin[:, 1])) if fin.size else 1.0
                height = max(height, 1.0)
            elif names != names0:
                rec['issues'].append('clip %d: скелет другой (%d костей vs %d)' % (N, len(names), len(names0)))
            nan = int(np.sum(~np.isfinite(Pw).all(-1) & ~np.isnan(Pw).all(-1)))
            sdev, sjump = _scale_spikes(jbb)
            step = 0.0
            if len(Pw) > 1:
                dd = np.linalg.norm(np.diff(Pw, axis=0), axis=-1)
                dd = dd[np.isfinite(dd)]
                step = float(dd.max()) if dd.size else 0.0
            seam = _pose_dist(Pw[0], Pw[-1], np.ones(len(names), bool)) if len(Pw) else 0.0
            rec['clip'].append(dict(N=N, keys=int(len(Pw)), dur=round(float(jbb['duration']), 3), bones=len(names),
                                    nan=nan, scale_dev=round(sdev, 3), scale_jump=round(sjump, 3),
                                    max_step=round(step, 2), seam=round(seam, 2)))
            world[N] = Pw
            rec.setdefault('_names', {})[N] = names
        rec['height'] = round(height, 1)
        if 0 in world and len(names0 or []):
            mask = np.ones(len(names0), bool)
            idle = world[0]

            def worst_from_idle(target):
                # худший случай по всем кадрам idle (оригинал прерывает idle в любой момент)
                return max(_pose_dist(f, target, mask) for f in idle) if len(idle) else 0.0

            def best_from_idle(target):
                return min(_pose_dist(f, target, mask) for f in idle) if len(idle) else 0.0
            t = rec['trans']
            same = lambda x, y: rec['_names'][x] == rec['_names'][y]
            if 2 in world and same(0, 2):
                t['idle->2 worst'] = round(worst_from_idle(world[2][0]), 2)
                t['idle->2 best'] = round(best_from_idle(world[2][0]), 2)
            if 2 in world and 3 in world and same(2, 3):
                t['2end->3'] = round(_pose_dist(world[2][-1], world[3][0], mask), 2)
            if 3 in world and 4 in world and same(3, 4):
                t['3end->4'] = round(_pose_dist(world[3][-1], world[4][0], mask), 2)
            if 4 in world and same(4, 0):
                t['4end->idle0'] = round(_pose_dist(world[4][-1], idle[0], mask), 2)
        # пороги — доля роста модели (рывок кадра > 15%, шов петли idle > 5%, скачок масштаба > 0.5)
        H = rec['height']
        for c in rec['clip']:
            if c['nan']:
                rec['issues'].append('clip %d: NaN в %d ключах' % (c['N'], c['nan']))
            if c['max_step'] > 0.15 * H:
                rec['issues'].append('clip %d: рывок кадра %.1f (рост %.0f)' % (c['N'], c['max_step'], H))
            if c['scale_jump'] > 0.5:
                rec['issues'].append('clip %d: скачок масштаба %.2f' % (c['N'], c['scale_jump']))
        rec.pop('_names', None)
        res.append(rec)
    if out_path:
        with open(out_path, 'w', encoding='utf-8') as f:
            json.dump(res, f, ensure_ascii=False, indent=1)
    for r in res:
        if not r['model'].startswith('npc_'):
            continue
        print('%-28s H=%6.1f clips=%s %s %s' % (r['model'], r['height'], r['clips'],
              json.dumps(r['trans'], ensure_ascii=False), '; '.join(r['issues'])))


if __name__ == '__main__':
    main()
