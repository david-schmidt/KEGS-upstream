# KEGS-upstream
This repo is used to track Kent Dickey's KEGS project within GitHub.
It provides a basis for forking and creating a traditional patch for consideration by Kent.
The official page for KEGS is on [Sourceforge](https://kegs.sourceforge.net/).

Each branch tracks the original source to a release number - so you can see the exact
diff between one (or more) release to another.
Each release from Sourceforge is unpacked and committed as a new branch; a traditional git
diff to its parent is visible in the commit history.
Kent has expressed his desire to accept patches and specifically avoid GitHub, so this will
help bridge the gap for those of us that have made that transition and need to backport
to the older way of doing things.
