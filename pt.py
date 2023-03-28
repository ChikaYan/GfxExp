import os
import imageio
import numpy as np
from pathlib import Path

def cmd(command):
    print(command)
    os.system(command)

spp = 64

# for i in range(spp):

#     c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
#         -cam-pos -0.9, 0.93, 2.586 \
#         -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
#         -brightness 2.0 \
#         -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
#         -inst scene \
#         -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
#         -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
#         -exp_name pt{i:02d} -no_nrc -rnd_seed {i:05d} -max_path_len 20 \
#     "

#     cmd(c)

c = f"C:\\nonsys\\workspace\\GfxExp\\build\\bin\\Debug\\neural_radiance_caching.exe \
    -cam-pos -0.9, 0.93, 2.586 \
    -cam-roll 7.487 -cam-pitch -cam-yaw -cam-yaw 77.589 \
    -brightness 2.0 \
    -name scene -obj C:\\nonsys\\workspace\\GfxExp\\scene\\living_room\\living_room.obj 1.0 simple_pbr \
    -inst scene \
    -name rectlight0 -emittance 1200 0 0 -rectangle 0.1 0.1 \
    -begin-pos -0.2 1.05 2.586 -end-pos -0.2 1.05 3.0 -freq 5 -time 1.0 -inst rectlight0 \
    -exp_name pt_{spp} -no_nrc -max_path_len 20 -spp {spp} \
"

cmd(c)

out_path = Path(f'C:\\nonsys\\workspace\\GfxExp\\exp\\pt_spp{spp}\\imgs')
out_path.mkdir(exist_ok=True, parents=True)

for img_i in range(50):
    imgs = []
    for i in range(spp):
        # img_path = f"C:\\nonsys\\workspace\\GfxExp\\exp\\pt{i:02d}\\imgs\\{img_i:05d}.png"
        img_path = f"C:\\nonsys\\workspace\\GfxExp\\exp\\pt_{spp}\\imgs\\{img_i:05d}_{i:02d}.png"
        im = imageio.imread(img_path)
        imgs.append(im)

    imgs = np.stack(imgs) / 255.
    im = (imgs.mean(axis=0) * 255.).astype(np.uint8)

    imageio.imwrite(str(out_path / f'{img_i:05d}.png'), im)


