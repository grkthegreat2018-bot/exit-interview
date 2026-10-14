import pathlib, re
# Swap getenv -> DevEnv only in per-frame paths (draw/update/run loops).
# One-shot getenvs in Init/Load stay as-is.
per_frame = {
    'src/game.cpp': ['EI_MENU', 'EI_PAUSE', 'EI_DIAG', 'EI_DUMPRT',
                     'EI_NOITEMS', 'EI_NOVIS', 'EI_DOORTRACE'],
    'src/office.cpp': ['EI_NOFURN', 'EI_FCUT', 'EI_NOCEIL'],
}
for path, names in per_frame.items():
    p = pathlib.Path(path)
    t = p.read_text(encoding='utf-8')
    for n in names:
        t = t.replace(f'std::getenv("{n}")', f'DevEnv("{n}")')
    p.write_text(t, encoding='utf-8')
    print(path, 'done')
