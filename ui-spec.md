# NAMberry UI Implementation Plan

This document describes the visual language and interaction model for the **NAMberry** plugin editor: a grounded, playful, stompbox-inspired interface built around a clear signal flow. The editor must feel like a tiny pedalboard and amp stack, not a dense rack of controls.

## Design Direction

### Core visual language
- Every effect block reads like a physical pedal.
- Controls sit on the top half of each pedal card.
- The bottom half is a stomp switch area with a larger bypass toggle.
- The signal chain is obvious from left to right: Pre FX → Amp → Post EQ → IR → Post FX.
- The amp section is a single amp-style block with a grille, control caps, and simplified knob layout.
- The UI is intentionally simple, readable, and fun without sacrificing usability.
- The design is inspired by classic amp front panels: dark metal chassis, centered grille, bright white labels, and a clean horizontal control row.

### Pedal card pattern
- Dark metal chassis with subtle highlight rings.
- Top area holds the knob/slider controls.
- Bottom area is a large bypass stomp pad.
- Bypass state changes the pad colour and glow.
- The card title sits in a compact lens label above the controls.

### Amp pattern
- A rectangular amp body with a perforated grille pattern.
- The preamp tone controls are represented as top-mounted knobs.
- Gain, Bass, Mid, Treble, Presence, Depth, and Output are laid out in a compact horizontal band.
- The bypass switch sits as the amp’s footswitch below the control row.
- The overall composition is intentionally similar to a hardware amp front panel: wide dark body, bright metal details, and white labeling that feels premium and tactile.

This is the current target design for the JUCE editor implementation.

---

## 1. Architectural Overview & Qt Integration

Using Qt 6 / QML allows for hardware-accelerated, high-DPI responsive graphics with a clean separation between the audio/DSP domain and the user interface.


```

┌─────────────────────────────────────────────────────────┐
│                      Qt 6 / QML UI                      │
│   (Custom Rotary Knobs, Visual EQs, File Drop, Tabs)    │
└───────────────────────────┬─────────────────────────────┘
│ Q_PROPERTY / Signal-Slot / QAbstractItemModel
┌───────────────────────────▼─────────────────────────────┐
│                 NAMberry C++ GUI Bridge                 │
│         (QtParamBridge / PresetManager / DragDrop)      │
└───────────────────────────┬─────────────────────────────┘
│ Atomic / Lock-Free Sync
┌───────────────────────────▼─────────────────────────────┐
│                 C++ Real-Time DSP Engine                │
│    (NAM Core, 4-Slot IR, MultiBand EQs, FX Modules)     │
└─────────────────────────────────────────────────────────┘

```

### Key Technical Strategies
* **Hardware Acceleration**: QML uses Scene Graph (RHI) to offload custom rendering (e.g., EQ curves, meters) to Vulkan, Metal, Direct3D 11/12, or OpenGL.
* **Thread Isolation**: The QML engine runs entirely on the GUI thread. Real-time safe C++ structures communicate parameter changes atomically without locking the audio thread.
* **Declarative Styling**: Modular QML controls with custom state triggers for bypass, active indicators, and drag-and-drop hit boxes.

---

## 2. Directory Structure


```

ui/
├── CMakeLists.txt
├── main.qml
├── qml.qrc
├── components/
│   ├── Knob.qml
│   ├── ToggleSwitch.qml
│   ├── MultiBandEqGraph.qml
│   ├── IrSlotControl.qml
│   ├── NamDropArea.qml
│   └── Meter.qml
├── views/
│   ├── PreChainView.qml
│   ├── AmpCabView.qml
│   └── PostFxView.qml
└── cpp_bridge/
├── QtParamBridge.h
├── QtParamBridge.cpp
├── FileDropHandler.h
└── FileDropHandler.cpp

```

---

## 3. C++ / QML Parameter Bridge

The C++ bridge exposes plugin parameters to QML using `QObject`, `Q_PROPERTY`, and reactive signals.

### 3.1 `QtParamBridge.h`

```cpp
#pragma once

#include <QObject>
#include <QVariantMap>

class QtParamBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float ampGain READ ampGain WRITE setAmpGain NOTIFY ampGainChanged)
    Q_PROPERTY(float ampPresence READ ampPresence WRITE setAmpPresence NOTIFY ampPresenceChanged)
    Q_PROPERTY(float ampDepth READ ampDepth WRITE setAmpDepth NOTIFY ampDepthChanged)
    Q_PROPERTY(float ampOutput READ ampOutput WRITE setAmpOutput NOTIFY ampOutputChanged)

public:
    explicit QtParamBridge(QObject *parent = nullptr);

    float ampGain() const { return m_ampGain; }
    float ampPresence() const { return m_ampPresence; }
    float ampDepth() const { return m_ampDepth; }
    float ampOutput() const { return m_ampOutput; }

public slots:
    void setAmpGain(float value);
    void setAmpPresence(float value);
    void setAmpDepth(float value);
    void setAmpOutput(float value);
    void loadNamModel(const QString &filePath);
    void loadIrFile(int slotIndex, const QString &filePath);

signals:
    void ampGainChanged(float value);
    void ampPresenceChanged(float value);
    void ampDepthChanged(float value);
    void ampOutputChanged(float value);
    void namModelLoaded(const QString &fileName);
    void irFileLoaded(int slotIndex, const QString &fileName);

private:
    float m_ampGain { 0.5f };
    float m_ampPresence { 0.5f };
    float m_ampDepth { 0.5f };
    float m_ampOutput { 0.8f };
};

```

---

## 4. Custom QML Components

### 4.1 Custom Rotary Knob (`ui/components/Knob.qml`)

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    property real value: 0.0          // 0.0 to 1.0
    property real minimumValue: 0.0
    property real maximumValue: 1.0
    property string label: "KNOB"
    property color accentColor: "#06b6d4"

    width: 70
    height: 90

    signal valueModified(real newValue)

    // Outer Track & Pointer
    Canvas {
        id: canvas
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 60
        height: 60

        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();

            var cx = width / 2;
            var cy = height / 2;
            var radius = 24;

            var startAngle = 0.75 * Math.PI;
            var endAngle = 2.25 * Math.PI;
            var currentAngle = startAngle + (root.value - root.minimumValue) / 
                               (root.maximumValue - root.minimumValue) * (endAngle - startAngle);

            // Background Arc
            ctx.beginPath();
            ctx.arc(cx, cy, radius, startAngle, endAngle, false);
            ctx.lineWidth = 4;
            ctx.strokeStyle = "#27272a";
            ctx.stroke();

            // Value Active Arc
            ctx.beginPath();
            ctx.arc(cx, cy, radius, startAngle, currentAngle, false);
            ctx.lineWidth = 4;
            ctx.strokeStyle = root.accentColor;
            ctx.stroke();

            // Rotary Cap Background
            ctx.beginPath();
            ctx.arc(cx, cy, radius - 6, 0, 2 * Math.PI, false);
            ctx.fillStyle = "#18181b";
            ctx.fill();
            ctx.lineWidth = 1;
            ctx.strokeStyle = "#3f3f46";
            ctx.stroke();

            // Pointer Line
            ctx.beginPath();
            ctx.moveTo(cx, cy);
            ctx.lineTo(cx + (radius - 8) * Math.cos(currentAngle), 
                       cy + (radius - 8) * Math.sin(currentAngle));
            ctx.lineWidth = 2;
            ctx.strokeStyle = root.accentColor;
            ctx.stroke();
        }

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
    }

    // Interactive Drag Region
    MouseArea {
        anchors.fill: canvas
        property real lastY: 0

        onPressed: lastY = mouseY
        onPositionChanged: {
            if (pressed) {
                var dy = lastY - mouseY;
                var step = (root.maximumValue - root.minimumValue) / 200.0;
                var newVal = Math.max(root.minimumValue, Math.min(root.maximumValue, root.value + dy * step));
                if (newVal !== root.value) {
                    root.value = newVal;
                    root.valueModified(newVal);
                    canvas.requestPaint();
                }
                lastY = mouseY;
            }
        }
    }

    // Label Text
    Text {
        anchors.top: canvas.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 4
        text: root.label
        color: "#a1a1aa"
        font.pixelSize: 10
        font.bold: true
    }
}

```

---

### 4.2 NAM Drop Target & Loader Box (`ui/components/NamDropArea.qml`)

```qml
import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: dropZone
    width: 280
    height: 90
    radius: 8
    color: dropArea.containsDrag ? "#27272a" : "#18181b"
    border.color: dropArea.containsDrag ? "#06b6d4" : "#3f3f46"
    border.width: 1

    property string currentModelName: "No .nam model loaded"

    signal fileDropped(string filePath)

    Column {
        anchors.centerIn: parent
        spacing: 6

        Text {
            text: "⚡ NAM MODEL LOADER"
            color: "#06b6d4"
            font.bold: true
            font.pixelSize: 11
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            text: dropZone.currentModelName
            color: "#f4f4f5"
            font.pixelSize: 12
            elide: Text.ElideMiddle
            width: dropZone.width - 20
            horizontalAlignment: Text.AlignHCenter
        }

        Text {
            text: "Drag & Drop .nam file here"
            color: "#71717a"
            font.pixelSize: 10
            anchors.horizontalCenter: parent.horizontalCenter
        }
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: (drop) => {
            if (drop.hasUrls) {
                var url = drop.urls[0];
                if (url.endsWith(".nam")) {
                    dropZone.fileDropped(url);
                    dropZone.currentModelName = url.substring(url.lastIndexOf('/') + 1);
                }
            }
        }
    }
}

```

---

## 5. Main Application View & Layout Container

### 5.1 `ui/main.qml`

```qml
import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "components"
import "views"

Window {
    width: 960
    height: 640
    visible: true
    title: "NAMberry - High Performance Amp & FX Suite"
    color: "#09090b"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header Navigation Bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: "#18181b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16

                Text {
                    text: "NAMBERRY"
                    color: "#ffffff"
                    font.bold: true
                    font.pixelSize: 16
                    font.letterSpacing: 2
                }

                Item { Layout.fillWidth: true }

                // Workspace Selector Tabs
                TabBar {
                    id: tabBar
                    background: Rectangle { color: "transparent" }

                    TabButton { text: "PRE-FX & TUNER" }
                    TabButton { text: "AMP & CAB" }
                    TabButton { text: "POST-FX" }
                }
            }
        }

        // View Stack
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            PreChainView {}
            AmpCabView {}
            PostFxView {}
        }
    }
}

```

---

## 6. Real-Time Performance & Threading Guidelines

1. **GUI vs Audio Isolation**: Never perform file parsing or allocation inside QML signal handlers directly affecting the audio loop. Pass file strings to `QtParamBridge`, which triggers background loading threads.
2. **Atomic Parameter Sync**: Map values modified in `Knob.qml` to `std::atomic<float>` buffers inside your C++ DSP engine.
3. **Canvas Redraw Batching**: Use `requestPaint()` only when mouse positions actually change values to maintain low GPU overhead during high audio performance.
