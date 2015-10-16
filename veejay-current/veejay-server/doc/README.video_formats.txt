Recommended format is MJPEG

Transcode your video with:

$ ffmpeg -i input-file -vcodec mjpeg -pix_fmt yuvj422p -q:v 0 output-file.avi

Optionally add PCM 16bit audio, 44.1/48.0 Khz, 2 channels, 8 bits per channel

ffmpeg -i input-file -vcodec mjpeg -s 128x128 -pix_fmt yuvj422p -acodec pcm_s16le -ar 44100 -ac 2 output-file.avi



Alternatively, create a fake v4l2 device and stream some video to it:

$ sudo modprobe v4l2loopback

$ ffmpeg -f x11grab -r 25 -s 720x576 -i :0.0+0,0 -vcodec rawvideo -pix_fmt yuv422p -f v4l2 /dev/video0

$ veejay -A1 -a0 
