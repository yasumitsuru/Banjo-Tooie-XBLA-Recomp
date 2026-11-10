"""MISSAO 02 - imprime a desmontagem (comentarios '// ...') de funcoes do codigo gerado pelo rexglue.
Uso: python -I func_asm.py <pasta generated/default> <0xENDERECO> [...]"""
import glob, os, re, sys
gen = sys.argv[1]
want = {a.lower().replace('0x', '').upper() for a in sys.argv[2:]}
for path in sorted(glob.glob(os.path.join(gen, 'banjotooie_recomp.*.cpp'))):
    cur = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = re.match(r'(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\(\s*(?:__imp__)?sub_([0-9A-F]{8})', line)
            if m:
                cur = m.group(1) if m.group(1) in want else None
                if cur:
                    print(f'===== sub_{cur} ({os.path.basename(path)})')
                continue
            if cur:
                s = line.strip()
                if s.startswith('// ') or s.startswith('loc_'):
                    print('  ' + s)
