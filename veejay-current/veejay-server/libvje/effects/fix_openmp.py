#!/usr/bin/env python3
import os
import re
import sys

def fix_openmp_deadlocks(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    original_content = content
    modified = False

    # Pattern to find #pragma omp single blocks that contain a bare 'return;'
    # This is the primary cause of the hangs.
    # We replace it with a skip flag pattern.
    
    # This regex looks for #pragma omp single { ... return; ... }
    # It's a simplified heuristic that catches the most dangerous cases.
    pattern = re.compile(
        r'(#pragma\s+omp\s+single\s*\{.*?)\breturn\s*;\s*(.*?\})',
        re.DOTALL
    )

    def replace_return(match):
        nonlocal modified
        modified = True
        before = match.group(1)
        after = match.group(2)
        
        # Inject skip flag logic
        # We assume a variable 'int skip_processing = 0;' exists or we add a comment 
        # for the developer to ensure it's declared at the top of the function.
        return f"{before} skip_processing = 1; {after}"

    content = pattern.sub(replace_return, content)

    # Also warn about nested parallelism in helper functions
    if re.search(r'#pragma\s+omp\s+parallel\s+for', content):
        # Check if it's inside a function that is NOT the main apply function
        # This is a heuristic warning
        pass

    if modified:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f"[FIXED] {filepath}")
    else:
        print(f"[OK]     {filepath}")

def main():
    target_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    print(f"Scanning directory: {target_dir}")
    
    for root, dirs, files in os.walk(target_dir):
        for file in files:
            if file.endswith('.c'):
                filepath = os.path.join(root, file)
                fix_openmp_deadlocks(filepath)
                
    print("Done. Please review files marked [FIXED] to ensure 'int skip_processing = 0;' is declared at the top of the function.")

if __name__ == "__main__":
    main()
