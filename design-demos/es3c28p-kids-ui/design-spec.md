# ES3C28P Kids UI — direction exploration spec

## Assumptions

This is a child-facing alternate skin for the existing ES3C28P firmware, not a separate product. The primary audience is assumed to be children around 5–10 years old using a 2.8-inch 320×240 resistive-size touch target environment, often without an adult standing beside them. English remains the default language. The core product capabilities stay unchanged: AI conversation, microphone state, SD-card music player and browser, volume controls, battery, Wi-Fi state and on-device Wi-Fi setup. The design must therefore reduce reading, enlarge touch targets, avoid dense menus and make current state obvious through shape and color.

## Content and interaction

- Chat is the home screen: one friendly assistant response, one user response, a large microphone control and an unmistakable listening/ready state.
- Music is the second primary screen: track title, elapsed/total time, progress, previous/play/next, and folder browser entry.
- Equal-width AI and Music tabs remain fixed at the bottom so the mental model does not change between themes.
- Status information remains available but visually secondary: Wi-Fi, volume and battery.
- Controls should have at least a 40×40 logical touch region. Avoid small icon-only actions unless their hit area is expanded.
- No advertising, rewards economy, streaks or fabricated statistics. This is a calm appliance interface, not an engagement game.

## Tone and visual constraints

The emotional target is safe, curious and cheerful without looking babyish. Rounded geometry is allowed, but every direction must have a distinct structural idea rather than being the existing Studio theme recolored. The 320×240 screen is the only canvas; no scrolling is allowed on primary Chat and Music screens. Use flat fills, strong contrast and no expensive gradients or external images so the selected direction can be reproduced in LVGL on ESP32-S3. Decorative elements must be CSS/LVGL-friendly primitives. Body text is at least 14 px in the prototype, with critical labels larger. The design should remain readable outdoors and under the photographed display’s lower contrast.

## Three exploration anchors

1. **Candy Controls** — roulette result 13, translated from Glassmorphism Bento into firmware-safe opaque bento tiles: energetic color, modular card composition and chunky controls.
2. **Storybook Garden** — reality reference: Sago Mini’s official product language of playful, intuitive exploration for young children, translated into warm flat scenery and a friendly abstract assistant rather than copied characters.
3. **Little Bauhaus** — custom studio direction inspired by Bauhaus learning blocks: primary geometry, high contrast, low visual noise and an age-flexible tone that can work beyond preschool.

## Output

Three standalone HTML prototypes, each rendering an exact 320×240 interface enlarged 3× for review. Each prototype includes a functional Chat/Music tab switch, visible status indicators and representative real firmware content. After the user selects one direction, the chosen visual system will be implemented as `CONFIG_ES3C28P_UI_STUDIO` or `CONFIG_ES3C28P_UI_KIDS` build-time variants. The existing Studio UI remains the default to avoid changing current builds unexpectedly.
