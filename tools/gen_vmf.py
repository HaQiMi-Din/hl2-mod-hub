#!/usr/bin/env python3
"""生成 mod_hub 中心大厅地图源文件 (mod_hub.vmf)。

一个轴对齐的房间（地板 + 四面墙 + 天花板），含玩家出生点与光照。
生成的 .vmf 需在 Hammer (Source SDK 2013) 中打开并编译为 .bsp。
用法: python3 tools/gen_vmf.py [输出路径]
"""

from __future__ import annotations

import sys
from pathlib import Path

MATERIAL = "dev/dev_measurewall01"


def box_faces(x0, y0, z0, x1, y1, z1, material=MATERIAL):
    """返回一个轴对齐盒子的 6 个面（每个面含 3 点平面 + 4 顶点 + 纹理轴）。"""
    v = lambda x, y, z: f"({x} {y} {z})"  # noqa: E731
    faces = []
    # (plane3, verts4, uaxis, vaxis)
    defs = [
        ((x1, y0, z0), (x1, y1, z0), (x1, y1, z1),  # +x
         [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
         "[0 1 0 0] 0.25", "[0 0 -1 0] 0.25"),
        ((x0, y0, z0), (x0, y0, z1), (x0, y1, z1),  # -x
         [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)],
         "[0 1 0 0] 0.25", "[0 0 -1 0] 0.25"),
        ((x0, y1, z0), (x1, y1, z0), (x1, y1, z1),  # +y
         [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)],
         "[1 0 0 0] 0.25", "[0 0 -1 0] 0.25"),
        ((x0, y0, z0), (x0, y0, z1), (x1, y0, z1),  # -y
         [(x0, y0, z0), (x0, y0, z1), (x1, y0, z1), (x1, y0, z0)],
         "[1 0 0 0] 0.25", "[0 0 -1 0] 0.25"),
        ((x0, y0, z1), (x1, y0, z1), (x1, y1, z1),  # +z (顶)
         [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
         "[1 0 0 0] 0.25", "[0 1 0 0] 0.25"),
        ((x0, y0, z0), (x0, y1, z0), (x1, y1, z0),  # -z (底)
         [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)],
         "[1 0 0 0] 0.25", "[0 1 0 0] 0.25"),
    ]
    for i, (p1, p2, p3, verts, u, vv) in enumerate(defs):
        sid = {
            "plane": f"{v(*p1)} {v(*p2)} {v(*p3)}",
            "material": material,
            "uaxis": u,
            "vaxis": vv,
            "verts": verts,
        }
        faces.append(sid)
    return faces


def brush_xml(brush_id, box, side_id_start=1):
    x0, y0, z0, x1, y1, z1 = box
    faces = box_faces(x0, y0, z0, x1, y1, z1)
    lines = [f"\tsolid\n\t{{\n\t\t\"id\" \"{brush_id}\""]
    for i, f in enumerate(faces):
        sid = side_id_start + i
        lines.append(f"\t\tside\n\t\t{{")
        lines.append(f"\t\t\t\"id\" \"{sid}\"")
        lines.append(f"\t\t\t\"plane\" \"{f['plane']}\"")
        lines.append(f"\t\t\t\"material\" \"{f['material']}\"")
        lines.append(f"\t\t\t\"uaxis\" \"{f['uaxis']}\"")
        lines.append(f"\t\t\t\"vaxis\" \"{f['vaxis']}\"")
        lines.append("\t\t\t\"rotation\" \"0\"")
        lines.append("\t\t\t\"lightmapscale\" \"16\"")
        lines.append("\t\t\t\"smoothing_groups\" \"0\"")
        lines.append("\t\t}")
    lines.append("\t}")
    return "\n".join(lines)


def entity_xml(eid, classname, origin, extra=None):
    lines = [f"\tentity\n\t{{", f"\t\t\"id\" \"{eid}\"", f"\t\t\"classname\" \"{classname}\""]
    if classname == "info_player_start":
        lines.append('\t\t"angles" "0 0 0"')
    lines.append(f'\t\t"origin" "{origin}"')
    for k, val in (extra or {}).items():
        lines.append(f'\t\t"{k}" "{val}"')
    lines.append("\t}")
    return "\n".join(lines)


def build_vmf(room_min=(0, 0, 0), room_max=(1024, 1024, 256), wall_t=16):
    x0, y0, z0 = room_min
    x1, y1, z1 = room_max
    # 六个面：地板/天花板 + 四面墙（厚度 wall_t）
    boxes = [
        (x0, y0, z0, x1, y1, z0 + wall_t),                              # 地板
        (x0, y0, z1 - wall_t, x1, y1, z1),                              # 天花板
        (x0, y0, z0 + wall_t, x0 + wall_t, y1, z1 - wall_t),            # -x 墙
        (x1 - wall_t, y0, z0 + wall_t, x1, y1, z1 - wall_t),            # +x 墙
        (x0 + wall_t, y0, z0 + wall_t, x1 - wall_t, y0 + wall_t, z1 - wall_t),  # -y 墙
        (x0 + wall_t, y1 - wall_t, z0 + wall_t, x1 - wall_t, y1, z1 - wall_t),  # +y 墙
    ]

    parts = [
        "versioninfo\n{",
        '\teditorversion\t400',
        '\teditorbuild\t10000',
        '\tmapversion\t4',
        '\tformatversion\t100',
        '\tprefab\t0',
        "}",
        "visgroups\n{\n}",
        "viewsettings\n{",
        '\tbSnapToGrid\t1',
        '\tbShowGrid\t1',
        '\tbShowLogicalGrid\t0',
        '\tnGridSpacing\t64',
        '\tbShow3DGrid\t0',
        "}",
        "world\n{",
        '\t"id" "1"',
        '\t"mapversion" "4"',
        '\t"classname" "worldspawn"',
        '\t"detailmaterial" "detail/detailsprites"',
        '\t"detailvbsp" "detail.vbsp"',
        '\t"maxpropscreenwidth" "-1"',
    ]
    brush_id = 1
    side_id = 1
    for box in boxes:
        parts.append(brush_xml(brush_id, box, side_id))
        brush_id += 1
        side_id += 6
    parts.append("}")
    parts.append("")

    cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    parts.append(entity_xml(100, "info_player_start",
                            f"{cx} {cy} {z0 + wall_t + 16}"))
    parts.append(entity_xml(101, "light_environment", f"{cx} {cy} {z1 - wall_t - 8}",
                            {"angles": "0 45 0",
                             "_light": "255 255 255 250",
                             "Ambient": "60 70 80 20",
                             "SunSpreadAngle": "10"}))
    parts.append(entity_xml(102, "env_sun", f"{cx} {cy} {z1 - wall_t - 8}",
                            {"angles": "0 45 0",
                             "HDRColorScale": "1.0"}))
    parts.append(entity_xml(103, "env_skypaint", f"{cx} {cy} {z1 - wall_t - 8}",
                            {"angles": "0 0 0",
                             "SkyColor": "100 120 150 255",
                             "SunColor": "255 255 200 255"}))
    parts.append("")
    return "\n".join(parts)


def main() -> int:
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parents[1] / "mod_hub" / "maps" / "mod_hub.vmf"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(build_vmf(), encoding="utf-8")
    print(f"VMF written: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
