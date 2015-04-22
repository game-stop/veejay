Recommended format is MJPEG

Transcode your video with:

ffmpeg -i input-file -vcodec mjpeg -pix_fmt yuvj422p -q:v 0 output-file.avi

Optionally add PCM 16bit audio, 44.1/48.0 Khz, 2 channels, 8 bits per channel
