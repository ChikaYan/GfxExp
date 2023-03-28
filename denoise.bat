SET FILE_EXTENSION=png
SET IMG_DIR=C:\nonsys\workspace\GfxExp\exp\mlp_2\imgs
SET PATH_TO_DENOISER=C:\nonsys\workspace\GfxExp\denoiser_v2.4

for /r %%v in (%IMG_DIR%/*.%FILE_EXTENSION%) do %PATH_TO_DENOISER%\Denoiser.exe -i "%%~nv.%FILE_EXTENSION%" -o "%%%~nv.%FILE_EXTENSION%"

cmd /k