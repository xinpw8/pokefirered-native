import sys

path = sys.argv[1]
with open(path, 'r') as f:
    content = f.read()

# Fix broken fprintf lines - they have literal newlines inside strings
# Pattern: fprintf(stderr, "...\n followed by a newline, then ", args);
import re

# Remove all the broken fprintf lines we added
lines = content.split('\n')
new_lines = []
skip_next = False
for i, line in enumerate(lines):
    if skip_next:
        skip_next = False
        continue
    # Skip lines that are our broken debug output
    if 'fprintf(stderr, "[YESNO]' in line and line.rstrip().endswith('='):
        # This is a broken line - skip it and next line
        skip_next = True
        continue
    if 'fprintf(stderr, "[YESNO]' in line and '\\n' not in line and line.rstrip().endswith('"'):
        # broken - skip  
        skip_next = True
        continue
    if line.strip().startswith('", (int)') or line.strip() == '");':
        # continuation of broken fprintf - skip
        continue
    new_lines.append(line)

content = '\n'.join(new_lines)

# Now add proper debug lines
# 1. In ScriptMenu_YesNo after gSpecialVar_Result = SCR_MENU_UNSET;
content = content.replace(
    '    gSpecialVar_Result = SCR_MENU_UNSET;\n\n    if (QL_AvoidDisplay',
    '    gSpecialVar_Result = SCR_MENU_UNSET;\n    fprintf(stderr, "[YESNO] ScriptMenu_YesNo called, ql=%d\\n", (int)gQuestLogState);\n\n    if (QL_AvoidDisplay'
)

# 2. YES selected
content = content.replace(
    '        case 0: // YES\n            gSpecialVar_Result = TRUE;',
    '        case 0: // YES\n            fprintf(stderr, "[YESNO] YES selected\\n");\n            gSpecialVar_Result = TRUE;'
)

# 3. NO selected  
content = content.replace(
    '            gSpecialVar_Result = FALSE;\n            break;\n        case 0: // YES',
    '            gSpecialVar_Result = FALSE;\n            fprintf(stderr, "[YESNO] NO selected (input=%d)\\n", (int)input);\n            break;\n        case 0: // YES'
)

# Make sure #include <stdio.h> is at top
if '#include <stdio.h>' not in content:
    content = '#include <stdio.h>\n' + content

with open(path, 'w') as f:
    f.write(content)

print("Patched successfully")
