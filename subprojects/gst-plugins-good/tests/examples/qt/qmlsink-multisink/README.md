# Memory leaks

## Missing unref(sink) ?



## Release component

There is memory leak on each videoitem instance, for each instance there are both GstCaps/GstPad that are detected alive.

    GST_TRACER :0:: object-alive, type-name=(string)GstCaps, address=(gpointer)0x7f6eac003280, description=(string)video/x-raw(memory:GLMemory), format=(string)RGBA, width=(int)320, height=(int)240, framerate=(fraction)30/1, multiview-mode=(string)mono, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, texture-target=(string)2D, ref-count=(uint)3, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstCaps, address=(gpointer)0x7f6ec0002ad0, description=(string)video/x-raw(memory:GLMemory), format=(string)RGBA, width=(int)320, height=(int)240, framerate=(fraction)30/1, multiview-mode=(string)mono, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, texture-target=(string)2D, ref-count=(uint)4, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstPad, address=(gpointer)0x5575a38e12e0, description=(string)<'':sink>, ref-count=(uint)1, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstPad, address=(gpointer)0x5575a38e0e40, description=(string)<'':sink>, ref-count=(uint)1, trace=(string);


### Stop/play

Each time pipeline status switch from stop to play a GstCaps is kept alive.

1. Stop and Quit

    GST_TRACER :0:: object-alive, type-name=(string)GstCaps, address=(gpointer)0x7f00b0002ad0, description=(string)video/x-raw(memory:GLMemory), format=(string)RGBA, width=(int)320, height=(int)240, framerate=(fraction)30/1, multiview-mode=(string)mono, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, texture-target=(string)2D, ref-count=(uint)4, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstPad, address=(gpointer)0x55eef9be72e0, description=(string)<'':sink>, ref-count=(uint)1, trace=(string);

2. Stop, Start and Quit

    GST_TRACER :0:: object-alive, type-name=(string)GstCaps, address=(gpointer)0x7f98b4002ad0, description=(string)video/x-raw(memory:GLMemory), format=(string)RGBA, width=(int)320, height=(int)240, framerate=(fraction)30/1, multiview-mode=(string)mono, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, texture-target=(string)2D, ref-count=(uint)4, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstCaps, address=(gpointer)0x7f98b4002f70, description=(string)video/x-raw(memory:GLMemory), format=(string)RGBA, width=(int)320, height=(int)240, framerate=(fraction)30/1, multiview-mode=(string)mono, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, texture-target=(string)2D, ref-count=(uint)2, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstPad, address=(gpointer)0x55bc4d9832e0, description=(string)<'':sink>, ref-count=(uint)1, trace=(string);

### Qt::Quit

Schedule RenderJob to unref sink is not executed if Qt::Quit(), only when loader is inactive

    GST_TRACER :0:: object-alive, type-name=(string)GstCaps, address=(gpointer)0x7fc284002ad0, description=(string)video/x-raw(memory:GLMemory), format=(string)RGBA, width=(int)320, height=(int)240, framerate=(fraction)30/1, multiview-mode=(string)mono, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, texture-target=(string)2D, ref-count=(uint)4, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstContext, address=(gpointer)0x56092e4f6860, description=(string)context 'gst.gl.GLDisplay'='context, gst.gl.GLDisplay=(GstGLDisplay)"\(GstGLDisplayX11\)\ gldisplayx11-0";', ref-count=(uint)1, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstGLContextGLX, address=(gpointer)0x7f6c480080f0, description=(string)<glcontextglx1>, ref-count=(uint)1, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstGLDisplayX11, address=(gpointer)0x56092e54e1e0, description=(string)<gldisplayx11-1>, ref-count=(uint)4, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstGLDisplayX11, address=(gpointer)0x56092e54e0e0, description=(string)<gldisplayx11-0>, ref-count=(uint)1, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstGLWindowX11, address=(gpointer)0x56092e5406a0, description=(string)<glwindowx11-1>, ref-count=(uint)1, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstGLWrappedContext, address=(gpointer)0x56092e55b190, description=(string)<glwrappedcontext0>, ref-count=(uint)1, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstPad, address=(gpointer)0x56092e3ed2e0, description=(string)<sink:sink>, ref-count=(uint)2, trace=(string);
    GST_TRACER :0:: object-alive, type-name=(string)GstQtSink, address=(gpointer)0x56092e4fd820, description=(string)<sink>, ref-count=(uint)1, trace=(string);

Why unref should be done asynchronously AfterSwapStage ?
