Recommended format is MJPEG

Transcode your video with:

ffmpeg -i input-file -vcodec mjpeg -pix_fmt yuvj422p -q:v 0 output-file.avi
