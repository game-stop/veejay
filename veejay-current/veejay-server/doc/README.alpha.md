```
 ______     __         ______   __  __     ______    
/\  __ \   /\ \       /\  == \ /\ \_\ \   /\  __ \   
\ \  __ \  \ \ \____  \ \  _-/ \ \  __ \  \ \  __ \  
 \ \_\ \_\  \ \_____\  \ \_\    \ \_\ \_\  \ \_\ \_\ 
  \/_/\/_/   \/_____/   \/_/     \/_/\/_/   \/_/\/_/ 
 ______     __  __     ______     __   __     __   __     ______     __        
/\  ___\   /\ \_\ \   /\  __ \   /\ "-.\ \   /\ "-.\ \   /\  ___\   /\ \       
\ \ \____  \ \  __ \  \ \  __ \  \ \ \-.  \  \ \ \-.  \  \ \  __\   \ \ \____  
 \ \_____\  \ \_\ \_\  \ \_\ \_\  \ \_\\"\_\  \ \_\\"\_\  \ \_____\  \ \_____\ 
  \/_____/   \/_/\/_/   \/_/\/_/   \/_/ \/_/   \/_/ \/_/   \/_____/   \/_____/ 
                                                                     in veejay
```
**Veejay has some support for alpha channel compositing**

In general, you will need to add an alpha channel to your playing sample or
stream by using one of the "Alpha:" filters in veejay.

Then, the alpha channel will be combined used FX that can deal with Alpha. Some
effects have a mode parameter `Alpha` that functions like an on/off switch but
others require an extra alpha channel to work.

By default, the alpha channel is set to `0` (completely invisible) so such effects
will always result in a black screen if you have not added an alpha channel.

In Reloaded, you can toggle the white button next to 'Alpha Clear' to switch 
between `0` (completely invisible) and `255` (completely visible)

Also, you can toggle the alpha-button to set the fade method to alpha blending
in the (manual) chain fader

The chain fader fades the original (unchanged) image to the image that is rendered
by the FX chain, either with an opacity value or by using alpha channel information 

In Reloaded, the chain fader is located next to the FX chain (the vertical slider).

_Alpha compositing is still in development and feedback is of course welcome_


List of "Alpha:" FX
-------------------
* **The 'Set by Color Key' operator**

Use this filter to create an alpha channel from the foreground object.

The filter requires an existing alpha channel to decide which pixel from source B to composite in. Pixels with an alpha of `0` are skipped.
Using the parameters `R,G,B` and `Angle` you can select which pixels belong to the background and key them out, leaving a mask of the foreground object.

* **The 'Select by Chroma Key' operator**

Use this filter to create an alpha channel from the foreground object.

The filter may use an existing alpha channel to decide which pixel from source B to composite in. Pixels with an alpha of `0` are skipped optionally.

* **Luma Key**

Luma Key in `Mode 3` will composite-in pixels from source B using its alpha channel

* **Alpha Blend**

Image in source B will be blended on top of source A using its alpha channel

* **Black and White Mask by Threshold**

This filter creates a black/white image from a minimum and maximum threshold value.

You can set a `Mode` parameter so that the render result is written as an Alpha channel

* **LVD Scale0Tilt / Crop,Scale,Tilt**

This filter is a port of the [frei0r](https://frei0r.dyne.org/) filter "scale0tilt.so"

You can use it to crop, scale, tilt an image from source B over source A
If the `Alpha` paramater is set to `1`, the final result will be an opaque pixel with the transparency of each pixel determined by the alpha channel values

* **Flatten Image**

Use this to multiply the alpha channel against a black background

* **Alpha Fill**

Solid value fill of the alpha channel into `0 - 255` range

* **Set from Image / Mixing source**

Use the luminance channel of the image as a new Alpha channel

You can use the FX switch parameter to scale the values to full range `0 - 255` if needed.
If you have no result in the Alpha channel, flip this parameter.

* **Alpha to Greyscale**

Use this FX to display the alpha channel as a greyscale image.

* **Transition Map**

Use this FX to blend over time using a greyscale image as an opacity map
