This isolated UI fixture uses Cage's headless wlroots backend, nested DomeD and
native virtual keyboard/pointer protocols. It neither starts an X server nor
connects input to the user's desktop. The helper requires an explicit private
Wayland socket; only test builds compile it, and it is never installed.

Unmodified MIT-licensed protocol definitions (copyright notices retained):
- https://raw.githubusercontent.com/atx/wtype/master/protocol/virtual-keyboard-unstable-v1.xml
- https://raw.githubusercontent.com/swaywm/wlr-protocols/master/unstable/wlr-virtual-pointer-unstable-v1.xml
