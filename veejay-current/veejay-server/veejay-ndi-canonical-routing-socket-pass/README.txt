VeeJay NDI canonical-source + patch-bay socket pass
==================================================

Apply on top of the previous routing-reconciliation + UTF-8 Director pass.

What this fixes
---------------
1. NDI green-wire connection could create a receiver object but never establish
   a real NDI connection, leaving the selected stream black.

   VeeJay now exposes the exact network identity returned by
   NDIlib_send_get_source_name() through VJNDI status as tx.source/tx.url.
   Director uses tx.source for direct VeeJay -> VeeJay NDI wiring.

2. Green drag from an NDI sender that is currently disabled is now transactional:

       enable NDI TX -> receive canonical VJNDI identity -> connect receiver

   Director no longer sends the receiver command immediately using the short
   pre-enable sender label.

3. Backend receiver has a fallback resolver for short sender labels. It searches
   NDI discovery for a unique exact/suffix match and uses the discovered full
   source name + URL. Ambiguous short labels fail instead of selecting randomly.

4. Patch-bay sockets are easier to grab:
   - fixed ~22 px capture radius independent of zoom
   - output sockets are hit-tested before node-move/background-pan
   - blue/green socket halo appears before mouse-down
   - cursor becomes a crosshair when the socket is captured
   - overlay reports "Ready to drag NDI TX/SHM/TCP OUT from <instance>"

5. One-shot NDI diagnostics:
   - sender logs its first submitted video frame
   - receiver logs its first delivered/decoded video frame

Expected successful test sequence
---------------------------------
After green-wiring program-1 -> veejay, logging should resemble:

  Publishing NDI source 'VeeJay program-1' as
      'veejay-machine (VeeJay program-1)' ...
  NDI sender 'veejay-machine (VeeJay program-1)' submitted first video frame
  NDI receiver created for 'veejay-machine (VeeJay program-1)' ...
  NDI source 'veejay-machine (VeeJay program-1)' is online
  NDI source 'veejay-machine (VeeJay program-1)' delivered first video frame ...

The previous immediate "source is offline" message should not occur while the
sender remains active.

Validation
----------
- patch generated against the previous complete working set
- git diff --check clean for every changed file
- replacement set contains only the six modified files

A full compilation was not possible in the artifact environment because the
complete VeeJay/GTK/NDI SDK development include tree is not installed there.
