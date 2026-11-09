# Кусок хаба из файлов игры в OBJ (меши хаба в игре нечитаемые — выгрузка из самой игры их не видит).
# python -I export_hub_area.py "<DUST>/Dust_Data/level1" x y z half out.obj  — x y z — точка в мировых координатах хаба,
# half — полуразмер куба в метрах. В OBJ x зеркально (правая система, как в Blender), цвета вершин — 0..1.
# Кусок сцены хаба (level1) вокруг точки -> OBJ (мировые координаты игры, x зеркально — правая система, как в Blender)
import sys, math, UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler
path, cx, cy, cz, half, out = sys.argv[1], *map(float, sys.argv[2:6]), sys.argv[6]
env = UnityPy.load(path)
objs = {o.path_id: o for o in env.objects}

def qmul(a, b):
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz)
def qrot(q, v):
    x, y, z, w = q; vx, vy, vz = v
    tx, ty, tz = 2*(y*vz-z*vy), 2*(z*vx-x*vz), 2*(x*vy-y*vx)
    return (vx+w*tx+y*tz-z*ty, vy+w*ty+z*tx-x*tz, vz+w*tz+x*ty-y*tx)

tcache = {}
def world(tr_pid):
    if tr_pid in tcache: return tcache[tr_pid]
    t = objs[tr_pid].read()
    p = (t.m_LocalPosition.x, t.m_LocalPosition.y, t.m_LocalPosition.z)
    r = (t.m_LocalRotation.x, t.m_LocalRotation.y, t.m_LocalRotation.z, t.m_LocalRotation.w)
    s = (t.m_LocalScale.x, t.m_LocalScale.y, t.m_LocalScale.z)
    par = t.m_Father.path_id
    if par and par in objs:
        pp, pr, ps = world(par)
        sp = (p[0]*ps[0], p[1]*ps[1], p[2]*ps[2])
        rp = qrot(pr, sp)
        res = ((pp[0]+rp[0], pp[1]+rp[1], pp[2]+rp[2]), qmul(pr, r), (ps[0]*s[0], ps[1]*s[1], ps[2]*s[2]))
    else:
        res = (p, r, s)
    tcache[tr_pid] = res
    return res

def inbox(v): return abs(v[0]-cx) < half and abs(v[1]-cy) < half and abs(v[2]-cz) < half

meshes = {}
def handler(mesh_obj):
    pid = (mesh_obj.assets_file.name, mesh_obj.path_id)
    if pid not in meshes:
        m = mesh_obj.read()
        h = MeshHandler(m); h.process()
        meshes[pid] = (m, h, h.get_triangles())
    return meshes[pid]

lines = [f'# DUST hub (level1) around {cx} {cy} {cz} +-{half} m; x mirrored (right-handed)']
base = 1; nobj = ntri = 0
for o in env.objects:
    if o.type.name != 'MeshRenderer': continue
    try:
        r = o.read()
        if not r.m_Enabled: continue
        go = r.m_GameObject.read()
        if not go.m_IsActive: continue
        mf = None
        for c in go.m_Components:
            co = c.component if hasattr(c, 'component') else c
            ref = co.deref() if hasattr(co, 'deref') else None
            if ref is not None and ref.type.name == 'MeshFilter': mf = ref.read(); break
        if mf is None or not mf.m_Mesh.path_id: continue
        mesh_obj = mf.m_Mesh.deref()
        mesh, h, tris_by_sub = handler(mesh_obj)
        sbi = getattr(r, 'm_StaticBatchInfo', None)
        first = sbi.firstSubMesh if sbi else 0
        count = sbi.subMeshCount if sbi else 0
        batched = count > 0
        subs = range(first, first+count) if batched else range(len(tris_by_sub))
        verts = h.m_Vertices
        if not batched:
            tr = None
            for c in go.m_Components:
                co = c.component if hasattr(c, 'component') else c
                ref = co.deref() if hasattr(co, 'deref') else None
                if ref is not None and ref.type.name == 'Transform': tr = ref.path_id; break
            wp, wr, ws = world(tr)
            def W(i):
                v = verts[i]; v = (v[0]*ws[0], v[1]*ws[1], v[2]*ws[2]); v = qrot(wr, v)
                return (v[0]+wp[0], v[1]+wp[1], v[2]+wp[2])
        else:
            def W(i): return tuple(verts[i])
        cols = h.m_Colors
        used = {}; faces = []
        for si in subs:
            if si >= len(tris_by_sub): continue
            for t in tris_by_sub[si]:
                ws3 = [W(i) for i in t]
                if not any(inbox(v) for v in ws3): continue
                for i, v in zip(t, ws3):
                    if i not in used: used[i] = (len(used), v)
                    faces.append(used[i][0])
        if not faces: continue
        lines.append(f"o {go.m_Name.replace(' ', '_')}")
        for i, (k, v) in sorted(used.items(), key=lambda kv: kv[1][0]):
            if cols is not None and len(cols) > i:
                c = cols[i]; k = 255.0 if max(c[0], c[1], c[2]) > 1.001 else 1.0; lines.append(f"v {-v[0]:.4f} {v[1]:.4f} {v[2]:.4f} {c[0]/k:.3f} {c[1]/k:.3f} {c[2]/k:.3f}")
            else:
                lines.append(f"v {-v[0]:.4f} {v[1]:.4f} {v[2]:.4f}")
        for k in range(0, len(faces), 3):
            lines.append(f"f {base+faces[k]} {base+faces[k+2]} {base+faces[k+1]}")
        base += len(used); nobj += 1; ntri += len(faces)//3
    except Exception as e:
        print('skip', o.path_id, type(e).__name__, e)
open(out, 'w').write('\n'.join(lines) + '\n')
print('objects', nobj, 'triangles', ntri, '->', out)
