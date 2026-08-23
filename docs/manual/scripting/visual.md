# Writing a script as a graph

A `.rvgraph` is a script you draw instead of type. It is not a third language:
saving one **writes C#** into the project's `Scripts/Generated/`, and the same
build that compiles your hand-written C# compiles it.

That is the whole design, and everything else on this page follows from it.
There is no interpreter, no bytecode and no second execution path — so a graph
gets live reload, the Build Log and restart-on-build for free, and the code it
produces is ordinary, readable, and yours to keep if you ever outgrow the
canvas.

```
Spinner.rvgraph  →  Scripts/Generated/Spinner.g.cs  →  the assembly  →  the Script component
```

## Making one

Select an entity, add a **Script** component, and set **Language** to
**Graph** — the third option beside C++ and C#. It asks for a name straight
away; press **Create** and the canvas opens on a working graph, with the
component already pointed at it.

The dialog has a second mode: **Pick existing** turns the name field into a
dropdown of the graphs already in the project, and the button becomes **Create
new** to go back. That is how one graph ends up on several entities.

The row underneath the Language row lists the project's graphs rather than its
C# classes, so switching an entity between two graphs is the same one-click
choice as switching between two scripts. **Edit Graph** reopens the canvas.

> [!NOTE]
> **One graph on many entities is one class with many instances.** Each entity
> keeps its own values for the script's fields, exactly as two entities running
> the same C# or C++ script do — editing a number on one does not touch the
> other. What is shared is the *graph*: change it, and every entity using it
> gets the new behaviour on the next build.

Graphs go in `assets/graphs/`. An existing one also opens by
**double-clicking it in the content browser**, where graphs have their own
icon.

The file name is the class name. `Spinner.rvgraph` generates
`public class Spinner : Script`, so renaming the file renames the class — and
the class name is what scene files store, the same rule as
[C++](cpp.md) and [C#](csharp.md).

> [!NOTE]
> A graph *runs* as C#, and the editor still says **Graph**, because those are
> different questions. Which language a component is written in is the
> engine's business; which of the three you chose is yours. The editor decides
> a component is a graph by finding `assets/graphs/<Name>.rvgraph` — the asset
> is the fact, rather than a flag in the scene file that could disagree with
> it.

What you start with is deliberately not an empty canvas:

```
On Create ──▶ Log
              ▲
   "Name ready" ─┘
```

Two nodes and a value — one execution wire and one data wire. That is the
smallest graph that shows both kinds, and the difference between them is the
one thing about a graph that cannot be guessed.

## The canvas

| | |
|---|---|
| **Left-drag empty space** | pan -- the graph moves under the cursor |
| **Right-click** | add a node, from a menu grouped by category |
| **Drag a pin** | wire it to another; the canvas refuses a wire it cannot make and says why |
| **Middle-drag** | pan, the same thing, for a mouse that makes it comfortable |
| **Ctrl or Shift + drag** | box select, and add what it covers to the selection |
| **Wheel** | zoom, between 25% and 250% |
| **Delete** | remove what is selected |
| **Ctrl+Z / Ctrl+Y** | undo, redo |
| **Ctrl+S** | save |

Hovering a pin names it and gives its type. As you zoom out, text drops away in
order — pin labels first, then the subtitle, then the title — so a graph you
have zoomed out of stays readable as shapes and category colours rather than
turning into unreadable smudges.

## Execution runs along the white wires

There are two kinds of wire, and the distinction is the one thing worth
understanding before anything else.

- **Execution** (white, and the pins are triangles) says
  *what happens and in what order*. It is what turns a picture into a sequence
  of statements.
- **Data** (coloured by type) says *what a value is*. Data wires have no order;
  they are read when the statement that needs them runs.

A node with no execution wire reaching it back to an event never runs. The
panel says so — that is the most common warning you will see, and it is worth
reading rather than dismissing.

## Events are where a graph starts

Ten of them, and they are the engine's own callbacks rather than invented ones:

**On Create**, **On Tick**, **On Frame**, **On Destroy**,
**On Collision Enter/Stay/Exit**, **On Trigger Enter/Stay/Exit**.

They mean exactly what they mean in [C++](cpp.md#the-lifecycle) and
[C#](csharp.md#the-lifecycle) — On Tick is the fixed step, On Frame is the
render frame. A graph may have as many events as it likes; each becomes one
`override`.

## What you can reach

146 nodes across nineteen categories, covering the same surface a written
script gets:

| Category | |
|---|---|
| Events, Flow, Functions | On Tick, Branch, Sequence, For Loop, While Loop, Break, Function, Call |
| Transform, Entity, Component | position, rotation, scale, parenting, find, spawn, destroy, named fields |
| Physics | forces, impulses, velocity, raycasts |
| Input, Time, Audio, UI, Output | actions by name, the step, one-shots, Log |
| Maths, Vector, Logic, Values | arithmetic, trigonometry, comparison, literals |
| Text | a number as text, text as a number, joining and comparing |
| Render | one setting by name, the graphics API, and whether an effect actually ran |
| Variables | numbers, vectors, flags, text and entities that survive between events |
| Containers | lists and string-keyed maps of numbers and entities |

**Text is how a graph says anything.** Until these existed a graph could
compute a frame rate and had no way whatsoever to display it: the only sources
of a string in the whole node set were a literal, a text variable and an
entity's name. **Number To Text** and **Join Text** are what turn a number into
something **Set UI Text** can take.

**Render Setting answers what was asked for; Feature Active answers what
happened.** They differ more often than they look: a project can ask for
ray-traced reflections and get the screen-space chain instead, because the
device has no ray query -- which is every OpenGL device, always. Anything
reporting to a person what the renderer is doing wants **Feature Active**, or
it will name a feature that never ran.

**Set Field writes the entity the script runs on; Set Field On writes one you
name.** The second exists because the first was the only one, and a graph could
not touch anything but itself where a C# script always could.

**Action Pressed and Action Released belong on On Tick.** The edge is raised
once per frame and cleared inside the fixed-step loop, which runs *before* On
Frame — so an On Frame that asks sees the press only on a frame that ran no
step. It looks like it works, at odds that fall with the frame rate. **Action
Down** is a state rather than an edge and is safe on either.

**Variables are the one thing a graph cannot work without.** A node graph with
no memory can only compute from its inputs, so anything that accumulates — a
timer, a counter, a position being moved rather than set — needs one. Each
becomes a private field on the generated class, which is exactly what makes it
survive from one tick to the next.

## Public, private, and what the inspector shows

The sidebar lists the graph's variables and functions under **Variables and
functions**, split into **Public** and **Private**. They are listed rather than
created there: a variable exists because a Get or Set node carries its name,
which is what makes the name mean anything. The section is where you say
something *about* one.

**The two sections are mutually exclusive, and the heading is the only place
that says which one a thing is in.** A variable moves between them with **Make
public** / **Make private** — an action rather than a checkbox, because a
checkbox beside a heading that already states the answer can only repeat it or
appear to disagree with it. Public means the generated field carries the
`public` modifier, so another script can reach it. Functions move the same way.

**ShowInEditor** is a checkbox, and it is a separate question: whether the
Script component puts a row on screen for that variable. It is orthogonal to
the section on purpose — a public variable can be hidden, which is the shape of
something another script drives, and a private one can be shown, which is the
ordinary tuning number.

The defaults are private and shown, which is what every graph had before these
existed, so nothing changes for a graph that says nothing.

> [!NOTE]
> **Shown is the default, and hiding is the opt-out.** The inspector reflects
> every instance field, private ones included — see
> [fields and the inspector](csharp.md#fields-and-the-inspector) — so clearing
> ShowInEditor emits `[HideInEditor]`, which hand-written C# can use too.

**Each entity keeps its own values.** Both public and private variables are
instance fields on the generated class, so two entities carrying the same graph
hold their own numbers: changing one on one entity does not touch the other.
That is not a rule graphs add — it is what two instances of any C# or C++
script already do.

## Types, and the one conversion allowed

Pins carry `Bool`, `Float`, `Vec3`, `String`, `Entity`, or a list or map of
numbers or entities. Wiring mismatched pins is refused as you drag, with the
reason on screen.

One conversion is automatic: **Bool, Float and Vec3 may feed a String input.**
That is not a convenience — the engine's named-field API is text, so without
it nothing but a string literal could ever reach **Set Field** and most of the
maths nodes would be unreachable. The reverse is refused, because parsing text
into a number can fail and a wire that might fail is not a wire.

## Problems: warnings and errors

The sidebar lists what is wrong with the graph as you edit it.

- A **warning** means the graph still generates, but something in it will not
  do anything — most often a node no execution wire reaches.
- An **error** means the graph does not generate at all.

> [!IMPORTANT]
> A graph with errors writes **no file**, and deletes the one it wrote last
> time. This is deliberate: an empty `class Foo : Script` compiles, attaches,
> runs and does nothing, so leaving a stale or empty script behind would mean
> the canvas showing four errors while the game happily ran the version from
> before them.

## Saving

**Ctrl+S** writes the graph *and* generates its C#, together, so what the
canvas shows and what the project compiles cannot disagree. Then **File →
Build Scripts** (Ctrl+B) compiles it like any other script, after which the
class appears in the Script component's dropdown — pick it, press Play.

`Scripts/Generated/` is worth committing: it makes a clone build without
opening the editor, and it makes a graph's effect reviewable as a diff.

> [!NOTE]
> Do not edit a generated `.g.cs`. It is rewritten every time the graph is
> saved, so an edit there survives until the next save and no longer. Change
> the graph — or, if you have outgrown it, delete the graph and keep the code.

## What a graph turns into

The generated file is ordinary C#. This is a real one, from the sample
project's `Spinner.rvgraph`:

```csharp
// Generated from Spinner.rvgraph. Do not edit.

using RageV;

public class Spinner : Script
{
    public override void OnCreate()
    {
        Log.Info("spinner ready");
    }

    public override void OnTick(float deltaTime)
    {
        var value8 = ((deltaTime * 180.0f) / 2.0f);

        if (value8 > 0.5f)
        {
            Entity.SetComponentField("TransformComponent", "Rotation", Text(value8));
            Log.Info("spun");
        }
        else
        {
            Log.Info("too slow");
        }
    }
}
```

Two things in there are worth pointing out because they are decisions rather
than accidents. A value read by more than one node is **spilled to a local**
(`value8`) rather than recomputed, so a graph cannot silently run the same
arithmetic twice. And the file is written in node-id order, so generating the
same graph twice produces the same bytes — a generated file whose contents
wander makes every save a diff nobody can read.

## A graph from a build that had more nodes

If a graph names a node type your build does not have — a project made with a
newer engine, or one where a node has since been removed — **the graph is
refused rather than partly loaded**, and the panel says which type it was.

The alternative is worse than it sounds. Dropping the unknown node and opening
the rest would generate a script missing that behaviour, overwrite the previous
one with it, and write the shortened graph back to disk the first time you
saved. Nothing on that path is reversible.

The refusal page offers **Open without it**, which does that same thing on
purpose and in the open: it says how many nodes and wires go, opens the canvas
*unsaved* so nothing has reached disk yet, and keeps a banner up until you
save. The problems list then tells you what was left dangling.

## Where to go next

- [Writing a script in C#](csharp.md) — what a graph generates, written by
  hand.
- [Writing a script in C++](cpp.md) — the other language, and the same
  lifecycle.
- [Component reference](../components.md#managedscriptcomponent) — the
  component that names the class a graph produces.
