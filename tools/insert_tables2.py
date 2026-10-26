# Writes tier_marks2.json (tier_marks2.py) into AirLabs/TrainerCore.h as the rank tables.
import json, sys
th = json.load(open(sys.argv[1] if len(sys.argv) > 1 else 'tier_marks2.json'))
names = {0: ["Small", "Medium", "Big", "Turnaround", "Mixed", "Flip Reset"],
         1: ["Precision", "Control", "Standard", "Fast", "Insane", "Dynamic Low", "Dynamic Medium", "Dynamic High", "Dynamic Full"],
         2: ["Control", "Fast", "Insane", "Dynamic Low", "Dynamic Medium", "Dynamic High", "Dynamic Full"],
         3: ["Slow", "Medium", "Fast", "Random"]}
counts = {0: 6, 1: 9, 2: 7, 3: 4}
arr = {0: "kTierFlick", 1: "kTierTrack", 2: "kTierHybrid", 3: "kTierChase"}
out = []
for m in range(4):
    out.append(f"    inline constexpr int {arr[m]}[{counts[m]}][kRangeCount][kTierCount - 1] = {{")
    for p in range(counts[m]):
        out.append(f"        {{ // {names[m][p]}")
        for r in range(3):
            key = f"{m} {p} {r}" if f"{m} {p} {r}" in th else f"{m} {p} 0"
            out.append("            { " + ", ".join(str(v) for v in th[key][1:]) + " },")
        out.append("        },")
    out.append("    };")
p = '/home/claude/AirLabs/AirLabs/TrainerCore.h'
s = open(p).read()
a = s.index('    inline constexpr int kTierFlick[')
b = s.index('    // The score needed for a tier on a preset')
s = s[:a] + "\n".join(out) + "\n\n" + s[b:]
open(p, 'w').write(s)
print("ok")
