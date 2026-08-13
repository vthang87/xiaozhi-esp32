# Direction approval

- Presented A: `direction-a-candy-controls.html` / `direction-a-candy-controls.png`
- Presented B: `direction-b-storybook-garden.html` / `direction-b-storybook-garden.png`
- Presented C: `direction-c-little-bauhaus.html` / `direction-c-little-bauhaus.png`
- User selection: “phương án b nhé , tôi muốn tách package theme có thể upload riêng không cần chung với core”
- Approved direction: **B — Storybook Garden**
- Implementation extension: theme data must be packaged and deployable independently from the firmware core.

The visual form comes from a small garden story scene: the assistant is a simple friendly character, the conversation is its speech card, and music controls sit on the landscape as large tactile objects. The production implementation will preserve this hierarchy using LVGL primitives and package-driven theme tokens rather than embedding the selected palette in core logic.
