Flow export from Flipper UI Studio.
flow-*.md: states and transitions; screens are linked images under screens/ (1:1, RGB565 colors, transparent = PNG alpha).
flow-*.json: states[] / transitions[] with triggers, animations, input modes and start/end flags; the schema is written at the
top of each file ("schema"). Linked screens carry mockup { id, name, file }. Flows > Import reads these files back.
Screens are the SAVED version of each asset, frame 1, all visible layers flattened.
templates.json: the screen template (zones) of every linked mockup; a state's mockup.template names it. Its format and the
centering rule are described inside the file.
