/**
 * Arguments every editor the test tooling launches carries.
 *
 * The source code accessor is set to one that opens nothing, so no test, no
 * dialog answer and no engine call (AddCodeToProject adds new files to the
 * solution through the accessor) can start an IDE on the developer's desktop.
 * On Windows the null accessor's module does not load, so the editor falls
 * back to its default accessor, which opens nothing either.
 */
export const NO_IDE_ARG = "-ini:EditorSettings:[/Script/SourceCodeAccess.SourceCodeAccessSettings]:PreferredAccessor=NullSourceCodeAccessor";

export const TEST_EDITOR_ARGS = [NO_IDE_ARG];
