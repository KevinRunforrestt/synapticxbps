## A Vibecoded Port of Synaptic for Void Linux


## FAQ

**Why does this exist?**

Because I'm bored and I like to experiment

**Is this stable?**

Tell me

**Is it worth using daily?**

I don't know, try it yourself

## Build instruccions

```
sudo xbps-install -S
sudo xbps-install -y base-devel gcc gtk+3-devel meson ninja pkg-config gettext polkit-devel libvte3-devel
```

```
meson setup build -Dxbps=enabled -Dtests=disabled

ninja -C build

# For installing
sudo cp build/gtk/synaptic /usr/bin/synaptic-xbps
```
