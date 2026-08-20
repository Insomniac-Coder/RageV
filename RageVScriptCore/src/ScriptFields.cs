using System;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;

namespace RageV;

/// <summary>
/// Keeps a field out of the inspector, without changing what the field is.
/// </summary>
/// <remarks>
/// The inspector shows every instance field of a supported type, private ones
/// included, and that default is deliberate -- see <c>Editable</c>. This is how
/// a script says "not this one": working state, a cache, something another
/// script drives. The alternatives were <c>static</c> and <c>readonly</c>, and
/// both of those change the field rather than how it is presented.
///
/// A visual graph emits it for any variable whose ShowInEditor box is clear
/// (10.13); hand-written C# can use it directly.
/// </remarks>
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = true)]
public sealed class HideInEditorAttribute : Attribute
{
}

/// <summary>What kind of thing a script field is. Matches the native enum.</summary>
public enum ScriptFieldType
{
	Unsupported = -1,
	Bool = 0,
	Int = 1,
	Float = 2,
	String = 3,
	Vector3 = 4,
}

/// <summary>
/// Reading and writing a script's public fields from the editor.
/// </summary>
/// <remarks>
/// Values cross the boundary as **text**, in the invariant culture. That looks
/// lazy and is deliberate: the scene file is text, the inspector edits text-like
/// values anyway, and the alternative is a tagged union that has to be kept in
/// step in two languages for the sake of avoiding a `float.Parse`. A field is
/// touched when somebody types in it, not per step, so the cost is nothing.
///
/// Invariant culture specifically. `float.ToString()` on a machine with a comma
/// decimal separator writes "1,5", and a scene saved there would not load
/// anywhere else -- which is the kind of bug that only shows up after someone
/// else opens your project.
///
/// Only fields with a supported type are listed. An unsupported field is not an
/// error: a script may legitimately have state the inspector has no way to show,
/// and it keeps whatever the constructor gave it.
/// </remarks>
public static unsafe class ScriptFields
{
	/// <summary>How many editable fields a script type has, or -1 if the type is unknown.</summary>
	[UnmanagedCallersOnly]
	public static int GetFieldCount(byte* typeName)
	{
		try
		{
			Type? type = ScriptHost.FindScriptType(Marshal.PtrToStringUTF8((IntPtr)typeName));
			if (type is null)
				return -1;

			int count = 0;
			foreach (FieldInfo field in Editable(type))
				count++;
			return count;
		}
		catch { return -1; }
	}

	/// <summary>
	/// Describes one field: writes "name\ndefault" into the buffer and returns
	/// its <see cref="ScriptFieldType"/>.
	/// </summary>
	/// <remarks>
	/// Two strings in one call, separated by a newline, because the alternative
	/// is either two round trips per field or an out-parameter for every piece.
	/// Neither is worth it for something the inspector reads once when a script
	/// is chosen.
	///
	/// The default comes from a throwaway instance, so it is whatever the field
	/// initialiser says rather than whatever zero happens to be. A script with
	/// `private float m_Speed = 1.2f;` should show 1.2 before anybody edits it.
	/// </remarks>
	[UnmanagedCallersOnly]
	public static int DescribeField(byte* typeName, int index, byte* buffer, int capacity)
	{
		try
		{
			Type? type = ScriptHost.FindScriptType(Marshal.PtrToStringUTF8((IntPtr)typeName));
			if (type is null)
				return (int)ScriptFieldType.Unsupported;

			FieldInfo? field = null;
			int seen = 0;
			foreach (FieldInfo candidate in Editable(type))
			{
				if (seen++ == index)
				{
					field = candidate;
					break;
				}
			}

			if (field is null)
				return (int)ScriptFieldType.Unsupported;

			string defaultValue = string.Empty;
			try
			{
				object? sample = Activator.CreateInstance(type);
				if (sample is not null)
					defaultValue = Format(field.GetValue(sample)) ?? string.Empty;
			}
			catch
			{
				// A script whose constructor throws still has fields worth
				// listing; it simply has no defaults to show.
			}

			Write(buffer, capacity, field.Name + "\n" + defaultValue);
			return (int)TypeOf(field.FieldType);
		}
		catch { return (int)ScriptFieldType.Unsupported; }
	}

	/// <summary>Reads a field off a live instance, as text. Returns the length, or -1.</summary>
	[UnmanagedCallersOnly]
	public static int GetFieldValue(int handle, byte* name, byte* buffer, int capacity)
	{
		try
		{
			Script? instance = ScriptHost.Find(handle);
			string? fieldName = Marshal.PtrToStringUTF8((IntPtr)name);
			if (instance is null || string.IsNullOrEmpty(fieldName))
				return -1;

			FieldInfo? field = Lookup(instance.GetType(), fieldName);
			if (field is null)
				return -1;

			return Write(buffer, capacity, Format(field.GetValue(instance)) ?? string.Empty);
		}
		catch { return -1; }
	}

	/// <summary>Writes a field on a live instance from text. Returns 1 on success.</summary>
	[UnmanagedCallersOnly]
	public static int SetFieldValue(int handle, byte* name, byte* value)
	{
		try
		{
			Script? instance = ScriptHost.Find(handle);
			string? fieldName = Marshal.PtrToStringUTF8((IntPtr)name);
			string? text = Marshal.PtrToStringUTF8((IntPtr)value);
			if (instance is null || string.IsNullOrEmpty(fieldName) || text is null)
				return 0;

			FieldInfo? field = Lookup(instance.GetType(), fieldName);
			if (field is null)
				return 0;

			object? parsed = Parse(field.FieldType, text);
			if (parsed is null)
				return 0;

			field.SetValue(instance, parsed);
			return 1;
		}
		catch { return 0; }
	}

	// --- the rules ----------------------------------------------------------

	/// <summary>
	/// Which fields the inspector may show: instance fields of a supported
	/// type, public or private.
	/// </summary>
	/// <remarks>
	/// Private ones included on purpose, and <see cref="HideInEditorAttribute"/>
	/// is how a field opts back out. The engine's own example scripts write
	/// `private float m_Speed = 1.2f;`, which is the right way to write C# and
	/// would otherwise be the one thing nobody can tune. Unity solves this with
	/// a `[SerializeField]` attribute; this solves it by not requiring one,
	/// which is fewer concepts for the same result. A field that genuinely must
	/// not be edited can be `static` or `readonly`, both of which are skipped.
	/// </remarks>
	private static System.Collections.Generic.IEnumerable<FieldInfo> Editable(Type type)
	{
		FieldInfo[] fields = type.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
		foreach (FieldInfo field in fields)
		{
			if (field.IsInitOnly || field.IsLiteral)
				continue;
			if (field.IsDefined(typeof(HideInEditorAttribute), inherit: true))
				continue;
			if (TypeOf(field.FieldType) == ScriptFieldType.Unsupported)
				continue;
			yield return field;
		}
	}

	private static FieldInfo? Lookup(Type type, string name)
	{
		foreach (FieldInfo field in Editable(type))
		{
			if (field.Name == name)
				return field;
		}
		return null;
	}

	private static ScriptFieldType TypeOf(Type type)
	{
		if (type == typeof(bool)) return ScriptFieldType.Bool;
		if (type == typeof(int)) return ScriptFieldType.Int;
		if (type == typeof(float)) return ScriptFieldType.Float;
		if (type == typeof(string)) return ScriptFieldType.String;
		if (type == typeof(Vector3)) return ScriptFieldType.Vector3;
		return ScriptFieldType.Unsupported;
	}

	private static string? Format(object? value)
	{
		return value switch
		{
			bool flag => flag ? "true" : "false",
			int number => number.ToString(CultureInfo.InvariantCulture),
			float number => number.ToString("R", CultureInfo.InvariantCulture),
			string text => text,
			Vector3 vector => string.Format(CultureInfo.InvariantCulture, "{0:R} {1:R} {2:R}",
											vector.X, vector.Y, vector.Z),
			_ => null,
		};
	}

	private static object? Parse(Type type, string text)
	{
		try
		{
			if (type == typeof(bool))
				return text == "true" || text == "1";

			if (type == typeof(int))
				return int.Parse(text, CultureInfo.InvariantCulture);

			if (type == typeof(float))
				return float.Parse(text, CultureInfo.InvariantCulture);

			if (type == typeof(string))
				return text;

			if (type == typeof(Vector3))
			{
				string[] parts = text.Split(' ', StringSplitOptions.RemoveEmptyEntries);
				if (parts.Length != 3)
					return null;
				return new Vector3(float.Parse(parts[0], CultureInfo.InvariantCulture),
								   float.Parse(parts[1], CultureInfo.InvariantCulture),
								   float.Parse(parts[2], CultureInfo.InvariantCulture));
			}
		}
		catch
		{
			// A half-typed number in the inspector is not an error worth
			// logging every keystroke; the field simply keeps its old value.
		}
		return null;
	}

	// The same length-that-would-have-fit contract the rest of the boundary
	// uses, so a caller that was truncated can tell.
	private static int Write(byte* buffer, int capacity, string text)
	{
		byte[] bytes = System.Text.Encoding.UTF8.GetBytes(text);
		if (buffer is not null && capacity > 0)
		{
			int copied = Mathf.Min(bytes.Length, capacity - 1);
			Marshal.Copy(bytes, 0, (IntPtr)buffer, copied);
			buffer[copied] = 0;
		}
		return bytes.Length;
	}
}
