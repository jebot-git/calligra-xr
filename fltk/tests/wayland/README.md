This isolated UI fixture uses Cage's headless wlroots backend, nested DomeD and
native virtual keyboard/pointer protocols. It neither starts an X server nor
connects input to the user's desktop. The helper requires an explicit private
Wayland socket; only test builds compile it, and it is never installed.

Unmodified MIT-licensed protocol definitions (copyright notices retained):
- https://raw.githubusercontent.com/atx/wtype/master/protocol/virtual-keyboard-unstable-v1.xml
- https://raw.githubusercontent.com/swaywm/wlr-protocols/master/unstable/wlr-virtual-pointer-unstable-v1.xml

Precise pointer fixtures should start `cage -d -- …`. A decorated nested host
can retain output extents that differ from its captured client area (observed
1288×720 versus1280×688); normalizing virtual pointer input to that capture
then misses small handles. Cage's `-d` removes the host decorations without
changing the application's hit regions or touching the running desktop. The
transform fixture records requested and delivered client coordinates to catch
this mismatch; geometric expectations allow only one output pixel of input
quantization (propagated through the final center-rotation calculation).
