Performance concerns with veejay
================

Veejay's performance depends much on the memory bandwith, CPU and disk access.

Has a live performance tools, veejay experience have to be reactive. You can achieve that
finding the right balance between your hardware capacities, the video resolution, the complexity of the effects chain.

For HDTV (1280x720) resolution with mjpeg codec, you need at least a 2.5 ghz. The **faster the better**.

For full PAL / NTSC resolutions (720x576 resp. 720x480), DV / Mjpeg  you need at least a 1.5 ghz,
for **lower resolution** (352x288) you can do fine with a 500-800 mhz PC.

Your **best bet** is working in RAW or MLZO (compressed) YUV `4:2:0` / `4:2:2`. On my pentium 4 , 3.0 ghz playing a AVI file that contains RAW YUV frames 
uses about 3-4% for a full PAL movie and 10-12% for mixing 2 movies.

The tradeoff here is your diskspeed. You could use compression, this **reduces the
size** of the videofile anywhere between 0-30% .

Typical for **laptops is slow disk** speed access, using a SSD device storage is a good option.
You can test your hard drive with `$ hdparm -T -t /dev/hdX`

If you **need to record** without framedrop, you can do so by disabling audio and
disabling synchronization with the command line options `-a0 -c0`.
