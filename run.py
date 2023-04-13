import os
from py_script.log_parser import log_parse
import yaml

def cmd(command):
    print(command)
    os.system(command)

CONF_NAME = "bistro/nrc64"
CONF_NAME = "bistro/short_path/nrc_only_emit"
# CONF_NAME = "bistro/nrc_only_emit"
# CONF_NAME = "living_room/nrc_only_tri"

EXE_PATH = "C:/nonsys/workspace/GfxExp/build/bin/Debug/neural_radiance_caching.exe"
CONF_PATH = f"configs/{CONF_NAME}.yaml"
EXP_NAME = CONF_NAME

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



