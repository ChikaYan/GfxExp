import os
from py_script.log_parser import log_parse
import yaml
from pathlib import Path

def cmd(command):
    print(command)
    os.system(command)


confs = [
    # "bistro/separate/test",
    # "bistro/separate/nrc",
    # "bistro/separate/nrc_only",
    # "bistro/separate/nrc_only_diffuse",
    # "bistro/separate/nrc_only_specular",
    # "bistro/nrc_gt_check_64",
    # "bistro/fre_exp/hash_2",
    # "bistro/fre_exp/hash_4",
    # "bistro/fre_exp/hash_16",
    # "bistro/fre_exp/tri_2",
    # "bistro/fre_exp/tri_6",
    # "bistro/fre_exp/tri_12",


    # "cornell_box/fre_exp/hash_2",
    # "cornell_box/fre_exp/hash_4",
    # "cornell_box/fre_exp/hash_16",
    # "cornell_box/fre_exp/tri_2",
    # "cornell_box/fre_exp/tri_6",
    # "cornell_box/fre_exp/tri_12",
    # "cornell_box/test",
    # "cornell_box/pt_1024",
    # "cornell_box/nrc",
    # "cornell_box/separate/test",
    # "cornell_box/nrc_only_emit",
    # "cornell_box/separate/nrc",
    # "cornell_box/separate/nrc_only_emit",
    # "cornell_box/separate/nrc_diffuse",
    # "cornell_box/separate/nrc_specular",

    # "cornell_box_dy/test",
    # "cornell_box_dy/pt_1024",
    # "cornell_box_dy/nrc_only_emit",
    # "cornell_box_dy/motion_tri/2",
    # "cornell_box_dy/motion_tri/6",
    # "cornell_box_dy/motion_tri/12",

    # "living_room_dy/test"
    # "living_room_dy/pt_512",
    # "living_room_dy/nrc_only_emit",
    # "living_room_dy/nrc_only_emit_spp_16",
    # "living_room_dy/motion_tri/2",
    # "living_room_dy/motion_tri/2_spp_16",
    # "living_room_dy/motion_tri/6",
    # "living_room_dy/motion_tri/12",

    # "living_room_dy_tex/test"
    # "living_room_dy_tex/pt_512",
    # "living_room_dy_tex/nrc_only_emit",
    # "living_room_dy_tex/nrc_only_emit_2",
    # "living_room_dy_tex/nrc_only_emit_spp_16",
    # "living_room_dy_tex/motion_tri/2",
    # "living_room_dy_tex/motion_tri/2_spp_16",
    # "living_room_dy_tex/motion_tri/6",
    # "living_room_dy_tex/motion_tri/12",
    # "living_room_dy_tex/nrc_only_emit_warp",
    # "living_room_dy_tex/nrc_only_emit_spp_16_warp",
    # "living_room_dy_tex/motion_tri_warp/2",
    # "living_room_dy_tex/motion_tri_warp/2_spp_16",
    # "living_room_dy_tex/motion_tri_warp/6",
    # "living_room_dy_tex/motion_tri_warp/12",
    # "living_room_dy_tex/perturb_smooth/range_01",
    # "living_room_dy_tex/perturb_smooth/range_05",
    # "living_room_dy_tex/perturb_smooth/range_001",
    # "living_room_dy_tex/perturb_smooth/range_0001",
    # "living_room_dy_tex/perturb_smooth/range_0001_4",
    # "living_room_dy_tex/perturb_smooth/range_0001_16",
    # "living_room_dy_tex/perturb_smooth/range_0001_4_after50",
    # "living_room_dy_tex/perturb_smooth/range_0001_after50",
    # "living_room_dy_tex/other_smooth/",
    # "living_room_dy_tex/other_smooth/small_mlp",
    # "living_room_dy_tex/other_smooth/small_mlp_grid_lv_2",
    "living_room_dy_tex/other_smooth/d_1_w_16_lv_2_high_lr",
    "living_room_dy_tex/other_smooth/d_2_w_8_lv_2",
    "living_room_dy_tex/other_smooth/d_2_w_16_lv_2",
]

# conf_name = "bistro/test"
# conf_name = "bistro/separate/test"
# conf_name = "bistro/pt/pt2048"
# conf_name = "bistro/close/nrc_only_hash_low_res"
# conf_name = "bistro/hash_lv=4/nrc_tri_low_fre"

def run(conf_name):
    EXE_PATH = "C:/nonsys/workspace/GfxExp/build/bin/Debug/neural_radiance_caching.exe"
    CONF_PATH = f"configs/{conf_name}.yaml"
    EXP_NAME = conf_name

    # SCENE_PATH = "C:/nonsys/workspace/GfxExp/scene/Bistro_v5_2/Bistro_v5_2/BistroExterior.fbx"

    with open(CONF_PATH, 'r') as file:
        conf = yaml.safe_load(file)

    conf['exp_name'] = EXP_NAME
    # conf['object0']['obj'] = f"{SCENE_PATH} 1.0 simple_pbr"

    c = f"{EXE_PATH} "

    for arg in conf:
        if arg == "no-cmd":
            pass
        elif arg.startswith('object'):
            for a in conf[arg]:
                c += f"-{a} {conf[arg][a] if conf[arg] is not None else ''} "
        else:
            c += f"-{arg} {conf[arg] if conf[arg] is not None else ''} "

    cmd(c)

    with open(f"./exp/{conf['exp_name']}/cmd.txt", "w") as f:
        f.write(c)

    with open(f"./exp/{conf['exp_name']}/config.yaml", "w") as f:
        yaml.dump(conf, f)



    ex_conf = conf.get('no-cmd', {})
    if ex_conf.get('parse_tb', True):
        log_parse(
            exp_path=f"C:/nonsys/workspace/GfxExp/exp/{conf['exp_name']}", 
            gt_dir=ex_conf.get('gt_dir', None),
            )


for conf in confs:
    # import pdb; pdb.set_trace()
    if os.path.isdir("configs/" + conf):
        for c_p in Path("configs/" + conf).glob('*.yaml'):
            run(conf + '/' + c_p.stem)
    else:
        run(conf)
