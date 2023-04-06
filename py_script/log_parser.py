import numpy as np
from torch.utils.tensorboard import SummaryWriter
import pandas as pd
import argparse
from pathlib import Path
import shutil
import imageio
from tqdm import tqdm
import os
from dataclasses import dataclass


from skimage.metrics import structural_similarity as ssim_fn



def cmd(command, verbose=True):
    if verbose:
        print(command)
    os.system(command)

def log_parse(
        exp_path=None, no_clean_event=False, 
        denoiser_path = 'C:/nonsys/workspace/GfxExp/denoiser_v2.4/Denoiser.exe',
        gt_dir = "C:/nonsys/workspace/GfxExp/exp/pt_res_64_avg/denoised_imgs",
        log_gt=False
    ):

    exp_dir = Path(exp_path)

    # first denoise the images
    imgs_dir = exp_dir / 'imgs'
    img_list = sorted(list(imgs_dir.glob('*.png')))
    denoised_dir = exp_dir / 'denoised_imgs'


    if denoised_dir.exists():
        shutil.rmtree(str(denoised_dir))
    denoised_dir.mkdir()
    for img in img_list:
        os.system(f"{denoiser_path} -i {str(img)} -o {str(denoised_dir/img.name)}")


    event_dir = exp_dir / 'tb'

    if not no_clean_event and event_dir.exists():
        shutil.rmtree(str(event_dir))

    event_dir.mkdir(exist_ok=True)

    summary_writer = SummaryWriter(str(event_dir))


    denoised_img_list = sorted(list(denoised_dir.glob('*.png')))

    if not log_gt:
        csv_log = pd.read_csv(str(exp_dir/'log.csv'))
        headers = csv_log.columns
        for i in tqdm(csv_log.index):
            for j, h in enumerate(headers):
                summary_writer.add_scalar(h, csv_log.loc[i][j], global_step=i)

        for i in range(len(denoised_img_list)):
            img_idx = int(denoised_img_list[i].stem)

            dn_img = imageio.imread(denoised_img_list[i])[..., :3]
            summary_writer.add_image('frame', dn_img, global_step=img_idx, dataformats='HWC')
            img = imageio.imread(img_list[i])[..., :3]
            summary_writer.add_image('raw_frame', img, global_step=img_idx, dataformats='HWC')

            if Path(f'{gt_dir}\\{img_idx:05d}.png').exists():
                dn_gt = imageio.imread(f'{gt_dir}\\{img_idx:05d}.png')[..., :3]

                stats_dn = {}

                dn_img = dn_img / 255.
                img = img / 255.
                dn_gt = dn_gt / 255.
                
                dn_mse = ((dn_img - dn_gt)**2).mean()
                stats_dn['mse'] = dn_mse
                stats_dn['psnr'] = -10.0 * np.log10(dn_mse)
                stats_dn['ssim'] = ssim_fn((dn_img*255).astype(np.uint8), (dn_gt*255).astype(np.uint8), channel_axis=2, data_range=255)

                # compute flip
                cmd(f'python C:\\nonsys\\workspace\\GfxExp\\flip\\python\\flip.py \
                    --test {exp_dir}\\denoised_imgs\\{img_idx:05d}.png \
                    --reference {gt_dir}\\{img_idx:05d}.png \
                    -d C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_dir.name}\\flip -txt \
                    -b {img_idx:05d} -v 0', verbose=False)
                

                flip_txt_path = Path(f'C:\\nonsys\\workspace\\GfxExp\\exp\\{exp_dir.name}\\flip') / f'{img_idx:05d}.txt'
                with flip_txt_path.open('r') as f:
                    lines = f.readlines()
                    stats_dn['flip_mean'] = float(lines[0].split(':')[-1].strip())
                    stats_dn['flip_med'] = float(lines[1].split(':')[-1].strip())

                for k in stats_dn:
                    summary_writer.add_scalar(f'denoised/{k}', stats_dn[k], global_step=img_idx)

    else:
        for i in range(len(denoised_img_list)):
            img_idx = int(denoised_img_list[i].stem)

            denoised_img = imageio.imread(denoised_img_list[i])
            summary_writer.add_image('frame', denoised_img, global_step=img_idx, dataformats='HWC')
            img = imageio.imread(img_list[i])
            summary_writer.add_image('raw_frame', img, global_step=img_idx, dataformats='HWC')


if __name__ == 'main':
    parser = argparse.ArgumentParser()

    parser.add_argument('--exp_path', default='',
                        help='path to exp log dir')
    parser.add_argument('--denoiser_path', default='C:/nonsys/workspace/GfxExp/denoiser_v2.4/Denoiser.exe',
                        help='path to exp log dir')
    parser.add_argument('--no_clean_event', action='store_true', default=False,
                        help='Do not clean existing events')

    args = parser.parse_args()

    log_parse(exp_path=args.exp_path, denoiser_path=args.denoiser_path, no_clean_event=args.no_clean_event)