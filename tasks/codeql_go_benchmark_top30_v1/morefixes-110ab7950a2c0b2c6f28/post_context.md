# Patch-overlapping Go context after the fix

## `cmd/root.go` (changed lines (12, 365, 366, 367, 368, 369, 370))

```go
package cmd

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"text/template"
	"time"

	"knative.dev/func/cmd/templates"
	"knative.dev/func/k8s"

	"github.com/ory/viper"
	"github.com/spf13/cobra"
	"golang.org/x/term"
	"k8s.io/apimachinery/pkg/util/sets"
	"knative.dev/client/pkg/util"
	fn "knative.dev/func"
)

type RootCommandConfig struct {
	Name string // usually `func` or `kn func`
	Version
	NewClient ClientFactory
}

// NewRootCmd creates the root of the command tree defines the command name, description, globally
// available flags, etc.  It has no action of its own, such that running the
// resultant binary with no arguments prints the help/usage text.
func NewRootCmd(config RootCommandConfig) *cobra.Command {
	cmd := &cobra.Command{
		// Use must be set to exactly config.Name, as this field is overloaded to
		// be used in subcommand help text as the command with possible prefix:
		Use:           config.Name,
		Short:         "Serverless functions",
		SilenceErrors: true, // we explicitly handle errors in Execute()
		SilenceUsage:  true, // no usage dump on error
		Long: `Knative serverless functions

	Create, build and deploy Knative functions

SYNOPSIS
	{{.Use}} [-v|--verbose] <command> [args]

EXAMPLES

	o Create a Node function in the current directory
	  $ {{.Use}} create --language node .

	o Deploy the function defined in the current working directory to the
	  currently connected cluster, specifying a container registry in place of
	  quay.io/user for the function's container.
	  $ {{.Use}} deploy --registry quay.io.user

	o Invoke the function defined in the current working directory with an example
	  request.
	  $ {{.Use}} invoke

	For more examples, see '{{.Use}} [command] --help'.`,
	}

	// Environment Variables
	// Evaluated first after static defaults, set all flags to be associated with
	// a version prefixed by "FUNC_"
	viper.AutomaticEnv()       // read in environment variables for FUNC_<flag>
	viper.SetEnvPrefix("func") // ensure that all have the prefix

	// Flags
	// persistent flags are available to all subcommands implicitly
	// Note they are bound immediately here as opposed to other subcommands
	// because this root command is not actually executed during tests, and
	// therefore PreRunE and other event-based listeners are not invoked.
	cmd.PersistentFlags().BoolP("verbose", "v", false, "Print verbose logs ($FUNC_VERBOSE)")
	if err := viper.BindPFlag("verbose", cmd.PersistentFlags().Lookup("verbose")); err != nil {
		fmt.Fprintf(os.Stderr, "error binding flag: %v\n", err)
	}
	cmd.PersistentFlags().StringP("namespace", "n", "", "The namespace on the cluster used for remote commands. By default, the namespace func.yaml is used or the currently active namespace if not set in the configuration. (Env: $FUNC_NAMESPACE)")
	if err := viper.BindPFlag("namespace", cmd.PersistentFlags().Lookup("namespace")); err != nil {
		fmt.Fprintf(os.Stderr, "error binding flag: %v\n", err)
	}

	// Version
	cmd.Version = config.Version.String()
	cmd.SetVersionTemplate(`{{printf "%s\n" .Version}}`)

	// Client
	// Use the provided ClientFactory or default to NewClient
	newClient := config.NewClient
	if newClient == nil {
		newClient = NewClient
	}

	// Grouped commands
	groups := templates.CommandGroups{
		{
			Header: "Main Commands:",
			Commands: []*cobra.Command{
				NewBuildCmd(newClient),
				NewConfigCmd(defaultLoaderSaver),
				NewCreateCmd(newClient),
				NewDeleteCmd(newClient),
				NewDeployCmd(newClient),
				NewInfoCmd(newClient),
				NewInvokeCmd(newClient),
				NewLanguagesCmd(newClient),
				NewListCmd(newClient),
				NewRepositoryCmd(newClient),
				NewRunCmd(newClient),
				NewTemplatesCmd(newClient),
			},
		},
		{
			Header: "Other Commands:",
			Commands: []*cobra.Command{
				NewCompletionCmd(),
				NewVersionCmd(config.Version),
			},
		},
	}

	// Add all commands to the root command, and initialize
	groups.AddTo(cmd)
	groups.SetRootUsage(cmd, nil)

	return cmd
}

// Helpers
// ------------------------------------------

// interactiveTerminal returns whether or not the currently attached process
// terminal is interactive.  Used for determining whether or not to
// interactively prompt the user to confirm default choices, etc.
func interactiveTerminal() bool {
	return term.IsTerminal(int(os.Stdin.Fd()))
}

// bindFunc which conforms to the cobra PreRunE method signature
type bindFunc func(*cobra.Command, []string) error

// bindEnv returns a bindFunc that binds env vars to the named flags.
func bindEnv(flags ...string) bindFunc {
	return func(cmd *cobra.Command, args []string) (err error) {
		for _, flag := range flags {
			if err = viper.BindPFlag(flag, cmd.Flags().Lookup(flag)); err != nil {
				return
			}
		}
		return
	}
}

// deriveName returns the explicit value (if provided) or attempts to derive
// from the given path.  Path is defaulted to current working directory, where
// a function configuration, if it exists and contains a name, is used.
func deriveName(explicitName string, path string) string {
	// If the name was explicitly provided, use it.
	if explicitName != "" {
		return explicitName
	}

	// If the directory at path contains an initialized function, use the name therein
	f, err := fn.NewFunction(path)
	if err == nil && f.Name != "" {
		return f.Name
	}

	return ""
}

// deriveNameAndAbsolutePathFromPath returns resolved function name and absolute path
// to the function project root. The input parameter path could be one of:
// 'relative/path/to/foo', '/absolute/path/to/foo', 'foo' or ”.
func deriveNameAndAbsolutePathFromPath(path string) (string, string) {
	var absPath string

	// If path is not specified, we would like to use current working dir
	if path == "" {
		path = cwd()
	}

	// Expand the passed function name to its absolute path
	absPath, err := filepath.Abs(path)
	if err != nil {
		return "", ""
	}

	// Get the name of the function, which equals to name of the current directory
	pathParts := strings.Split(strings.TrimRight(path, string(os.PathSeparator)), string(os.PathSeparator))
	return pathParts[len(pathParts)-1], absPath
}

// deriveImage returns the same image name which will be used.
// I.e. if the explicit name is empty, derive one from the configured registry
// (registry plus username) and the function's name.
//
// This is calculated preemptively here in the CLI (prior to invoking the
// client), only in order to provide information to the user via the prompt.
// The client will calculate this same value if the image override is not
// provided.
//
// Derivation logic:
// deriveImage attempts to arrive at a final, full image name:
//
//	format:  [registry]/[username]/[functionName]:[tag]
//	example: quay.io/myname/my.function.name:tag.
//
// Registry can optionally be omitted, in which case DefaultRegistry
// will be prepended.
//
// If the image flag is provided, this value is used directly (the user supplied
// --image or $FUNC_IMAGE).  Otherwise, the function at 'path' is loaded, and
// the Image name therein is used (i.e. it was previously calculated).
// Finally, the default registry is used, which is prepended to the function
// name, and appended with ':latest':
func deriveImage(explicitImage, defaultRegistry, path string) string {
	if explicitImage != "" {
		return explicitImage // use the explicit value provided.
	}
	f, err := fn.NewFunction(path)
	if err != nil {
		return "" // unable to derive due to load error (uninitialized?)
	}
	if f.Image != "" {
		return f.Image // use value previously provided or derived.
	}
	// Use the func system's derivation logic.
	// Errors deriving result in an empty return
	derivedValue, _ := f.ImageName()
	return derivedValue
}

func envFromCmd(cmd *cobra.Command) (*util.OrderedMap, []string, error) {
	if cmd.Flags().Changed("env") {
		env, err := cmd.Flags().GetStringArray("env")
		if err != nil {
			return nil, []string{}, fmt.Errorf("Invalid --env: %w", err)
		}
		return util.OrderedMapAndRemovalListFromArray(env, "=")
	}
	return util.NewOrderedMap(), []string{}, nil
}

func mergeEnvs(envs []fn.Env, envToUpdate *util.OrderedMap, envToRemove []string) ([]fn.Env, int, error) {
	updated := sets.NewString()

	var counter int

	for i := range envs {
		if envs[i].Name != nil {
			value, present := envToUpdate.GetString(*envs[i].Name)
			if present {
				envs[i].Value = &value
				updated.Insert(*envs[i].Name)
				counter++
			}
		}
	}

	it := envToUpdate.Iterator()
	for name, value, ok := it.NextString(); ok; name, value, ok = it.NextString() {
		if !updated.Has(name) {
			n := name
			v := value
			envs = append(envs, fn.Env{Name: &n, Value: &v})
			counter++
		}
	}

	for _, name := range envToRemove {
		for i, envVar := range envs {
			if *envVar.Name == name {
				envs = append(envs[:i], envs[i+1:]...)
				counter++
				break
			}
		}
	}

	errMsg := fn.ValidateEnvs(envs)
	if len(errMsg) > 0 {
		return []fn.Env{}, 0, fmt.Errorf(strings.Join(errMsg, "\n"))
	}

	return envs, counter, nil
}

// setPathFlag ensures common text/wording when the --path flag is used
func setPathFlag(cmd *cobra.Command) {
	cmd.Flags().StringP("path", "p", ".", "Path to the project directory (Env: $FUNC_PATH)")
}

// getPathFlag returns the value of the --path flag.
// The special value '.' is returned as the abolute path to the current
// working directory.
func getPathFlag() string {
	path := viper.GetString("path")
	if path == "." {
		path = cwd()
	}
	return path
}

// cwd returns the current working directory or exits 1 printing the error.
func cwd() (cwd string) {
	cwd, err := os.Getwd()
	if err != nil {
		panic(fmt.Sprintf("Unable to determine current working directory: %v", err))
	}
	return cwd
}

type Version struct {
	// Date of compilation
	Date string
	// Version tag of the git commit, or 'tip' if no tag.
	Vers string
	// Hash of the currently active git commit on build.
	Hash string
	// Verbose printing enabled for the string representation.
	Verbose bool
}

// Return the stringification of the Version struct, which takes into account
// the verbosity setting.
func (v Version) String() string {
	if v.Verbose {
		return v.StringVerbose()
	}

	// Ensure that the value returned is parseable as a semver, with the special
	// value v0.0.0 as the default indicating there is no version information
	// available.
	if strings.HasPrefix(v.Vers, "v") {
		// TODO: this is the naive approach, perhaps consider actually parse it
		// using the semver lib
		return v.Vers
	}

	// Any non-semver value is invalid, and thus indistinguishable from a
	// nonexistent version value, so the default zero value of v0.0.0 is used.
	return "v0.0.0"
}

// StringVerbose returns the verbose version of the version stringification.
// The format returned is [semver]-[hash]-[date] where the special value
// 'v0.0.0' and 'source' are used when version is not available and/or the
// libray has been built from source, respectively.
func (v Version) StringVerbose() string {
	var (
		vers = v.Vers
		hash = v.Hash
		date = v.Date
	)
	if vers == "" {
		vers = "v0.0.0"
	}
	if hash == "" {
		hash = "source"
	}
	if date == "" {
		date = time.Now().Format(time.RFC3339)
	}
	funcVersion := fmt.Sprintf("%s-%s-%s", vers, hash, date)
	return fmt.Sprintf("Version: %s\n"+
		"SocatImage: %s\n"+
		"TarImage: %s", funcVersion,
		k8s.SocatImage,
		k8s.TarImage)
}

// surveySelectDefault returns 'value' if defined and exists in 'options'.
// Otherwise, options[0] is returned if it exists.  Empty string otherwise.
//
// Usage Example:
//
//	languages := []string{ "go", "node", "rust" },
//	survey.Select{
//	  Options: options,
//	  Default: surveySelectDefaut(cfg.Language, languages),
//	}
//
// Summary:
//
// This protects against an incorrectly initialized survey.Select when the user
// has provided a nonexistant option (validation is handled elsewhere) or
// when a value is required but there exists no defaults (no default value on
// the associated flag).
//
// Explanation:
//
// The above example chooses the default for the Survey (--confirm) question
// in a way that works with user-provided flag and environment variable values.
//
//	`cfg.Language` is the current value set in the config struct, which is
//	   populated from (in ascending order of precedence):
//	   static flag default, associated environment variable, or command flag.
//	`languages` are the options which are being used by the survey select.
//
// This cascade allows for the Survey questions to be properly pre-initialzed
// with their associated environment variables or flags.  For example,
// A user whose default language is set to 'node' using the global environment
// variable FUNC_LANGUAGE will have that option pre-selected when running
// `func create -c`.
//
// The 'survey' package expects the value of the Default member to exist
// in the 'Options' member.  This is not possible when user-provided data is
// allowed for the default, hence this logic is necessary.
//
// For example, when the user is using prompts (--confirm) to select from a set
// of options, but the associated flag either has an unrecognized value, or no
// value at all, without this logic the resulting select prompt would be
// initialized with this as the default value, and the act of what appears to
// be choose the first option displayed does not overwrite the invalid default.
// It could perhaps be argued this is a shortcoming in the survey package, but
// it is also clearly an error to provide invalid data for a default.
func surveySelectDefault(value string, options []string) string {
	for _, v := range options {
		if value == v {
			return v // The provided value is acceptable
		}
	}
	if len(options) > 0 {
		return options[0] // Sync with the option which will be shown by the UX
	}
	// Either the value is not an option or there are no options.  Either of
	// which should fail proper validation
	return ""
}

// defaultTemplatedHelp evaluates the given command's help text as a template
// some commands define their own help command when additional values are
// required beyond these basics.
func defaultTemplatedHelp(cmd *cobra.Command, args []string) {
	var (
		body = cmd.Long + "\n\n" + cmd.UsageString()
		t    = template.New("help")
		tpl  = template.Must(t.Parse(body))
	)
	var data = struct{ Name string }{Name: cmd.Root().Use}

	if err := tpl.Execute(cmd.OutOrStdout(), data); err != nil {
		fmt.Fprintf(cmd.ErrOrStderr(), "unable to display help text: %v", err)
	}
}
```

## `cmd/root_test.go` (changed lines (212, 213, 214, 215, 218, 219, 220, 221, 224, 225, 226, 227, 230, 231, 232, 233, 256, 257, 258, 259, 260, 261))

```go
package cmd

import (
	"bytes"
	"fmt"
	"io"
	"os"
	"reflect"
	"strings"
	"testing"

	"github.com/ory/viper"
	"knative.dev/client/pkg/util"

	fn "knative.dev/func"
	. "knative.dev/func/testing"
)

func TestRoot_PersistentFlags(t *testing.T) {
	tests := []struct {
		name          string
		args          []string
		skipNamespace bool
	}{
		{
			name: "provided as root flags",
			args: []string{"--verbose", "--namespace=namespace", "list"},
		},
		{
			name: "provided as sub-command flags",
			args: []string{"list", "--verbose", "--namespace=namespace"},
		},
		{
			name:          "provided as sub-sub-command flags",
			args:          []string{"repositories", "list", "--verbose"},
			skipNamespace: true, // NOTE: no sub-sub commands yet use namespace, so it is not checked.
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			_ = fromTempDirectory(t)

			cmd := NewCreateCmd(NewClient)                      // Create a function
			cmd.SetArgs([]string{"--language", "go", "myfunc"}) // providing language
			if err := cmd.Execute(); err != nil {               // fail on any errors
				t.Fatal(err)
			}

			// Assert the persistent variables were propagated to the Client constructor
			// when the command is actually invoked.
			cmd = NewRootCmd(RootCommandConfig{NewClient: func(cfg ClientConfig, _ ...fn.Option) (*fn.Client, func()) {
				if cfg.Namespace != "namespace" && !tt.skipNamespace {
					t.Fatal("namespace not propagated")
				}
				if cfg.Verbose != true {
					t.Fatal("verbose not propagated")
				}
				return fn.New(), func() {}
			}})
			cmd.SetArgs(tt.args)
			if err := cmd.Execute(); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func TestRoot_mergeEnvMaps(t *testing.T) {

	a := "A"
	b := "B"
	v1 := "x"
	v2 := "y"

	type args struct {
		envs     []fn.Env
		toUpdate *util.OrderedMap
		toRemove []string
	}
	tests := []struct {
		name string
		args args
		want []fn.Env
	}{
		{
			"add new var to empty list",
			args{
				[]fn.Env{},
				util.NewOrderedMapWithKVStrings([][]string{{a, v1}}),
				[]string{},
			},
			[]fn.Env{{Name: &a, Value: &v1}},
		},
		{
			"add new var",
			args{
				[]fn.Env{{Name: &b, Value: &v2}},
				util.NewOrderedMapWithKVStrings([][]string{{a, v1}}),
				[]string{},
			},
			[]fn.Env{{Name: &b, Value: &v2}, {Name: &a, Value: &v1}},
		},
		{
			"update var",
			args{
				[]fn.Env{{Name: &a, Value: &v1}},
				util.NewOrderedMapWithKVStrings([][]string{{a, v2}}),
				[]string{},
			},
			[]fn.Env{{Name: &a, Value: &v2}},
		},
		{
			"update multiple vars",
			args{
				[]fn.Env{{Name: &a, Value: &v1}, {Name: &b, Value: &v2}},
				util.NewOrderedMapWithKVStrings([][]string{{a, v2}, {b, v1}}),
				[]string{},
			},
			[]fn.Env{{Name: &a, Value: &v2}, {Name: &b, Value: &v1}},
		},
		{
			"remove var",
			args{
				[]fn.Env{{Name: &a, Value: &v1}},
				util.NewOrderedMap(),
				[]string{a},
			},
			[]fn.Env{},
		},
		{
			"remove multiple vars",
			args{
				[]fn.Env{{Name: &a, Value: &v1}, {Name: &b, Value: &v2}},
				util.NewOrderedMap(),
				[]string{a, b},
			},
			[]fn.Env{},
		},
		{
			"update and remove vars",
			args{
				[]fn.Env{{Name: &a, Value: &v1}, {Name: &b, Value: &v2}},
				util.NewOrderedMapWithKVStrings([][]string{{a, v2}}),
				[]string{b},
			},
			[]fn.Env{{Name: &a, Value: &v2}},
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, _, err := mergeEnvs(tt.args.envs, tt.args.toUpdate, tt.args.toRemove)
			if err != nil {
				t.Errorf("mergeEnvs() for initial vars %v and toUpdate %v and toRemove %v got error %v",
					tt.args.envs, tt.args.toUpdate, tt.args.toRemove, err)
			}
			if !reflect.DeepEqual(got, tt.want) {

				gotString := "{ "
				for _, e := range got {
					gotString += fmt.Sprintf("{ %s: %s } ", *e.Name, *e.Value)
				}
				gotString += "}"

				wantString := "{ "
				for _, e := range tt.want {
					wantString += fmt.Sprintf("{ %s: %s } ", *e.Name, *e.Value)
				}
				wantString += "}"

				t.Errorf("mergeEnvs() = got: %s, want %s", gotString, wantString)
			}
		})
	}
}

// TestRoot_CommandNameParameterized confirmst that the command name, as
// printed in help text, is parameterized based on the constructor parameters
// of the root command.  This allows, for example, to have help text correct
// when both embedded as a plugin or standalone.
func TestRoot_CommandNameParameterized(t *testing.T) {
	expectedSynopsis := "%v [-v|--verbose] <command> [args]"

	tests := []string{
		"func",    // standalone
		"kn func", // kn plugin
	}

	for _, testName := range tests {
		var (
			cmd = NewRootCmd(RootCommandConfig{Name: testName})
			out = strings.Builder{}
		)
		cmd.SetArgs([]string{}) // Do not use test command args
		cmd.SetOut(&out)
		if err := cmd.Help(); err != nil {
			t.Fatal(err)
		}
		if cmd.Use != testName {
			t.Fatalf("expected command Use '%v', got '%v'", testName, cmd.Use)
		}
		if !strings.Contains(out.String(), fmt.Sprintf(expectedSynopsis, testName)) {
			t.Logf("Testing '%v'\n", testName)
			t.Log(out.String())
			t.Fatalf("Help text does not include substituted name '%v'", testName)
		}
	}
}

func TestVerbose(t *testing.T) {
	tests := []struct {
		name   string
		args   []string
		want   string
		wantLF int
	}{
		{
			name:   "verbose as version's flag",
			args:   []string{"version", "-v"},
			want:   "Version: v0.42.0-cafe-1970-01-01",
			wantLF: 3,
		},
		{
			name:   "no verbose",
			args:   []string{"version"},
			want:   "v0.42.0",
			wantLF: 1,
		},
		{
			name:   "verbose as root's flag",
			args:   []string{"--verbose", "version"},
			want:   "Version: v0.42.0-cafe-1970-01-01",
			wantLF: 3,
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			viper.Reset()

			var out bytes.Buffer

			cmd := NewRootCmd(RootCommandConfig{
				Name: "func",
				Version: Version{
					Date: "1970-01-01",
					Vers: "v0.42.0",
					Hash: "cafe",
				}})

			cmd.SetArgs(tt.args)
			cmd.SetOut(&out)
			if err := cmd.Execute(); err != nil {
				t.Fatal(err)
			}

			outLines := strings.Split(out.String(), "\n")
			if len(outLines)-1 != tt.wantLF {
				t.Errorf("expected output with %v line breaks but got %v:", tt.wantLF, len(outLines)-1)
			}
			if outLines[0] != tt.want {
				t.Errorf("expected output: %q but got: %q", tt.want, outLines[0])
			}
		})
	}
}

// Helpers
// -------

// pipe the output of stdout to a buffer whose value is returned
// from the returned function.  Call pipe() to start piping output
// to the buffer, call the returned function to access the data in
// the buffer.
func piped(t *testing.T) func() string {
	t.Helper()
	var (
		o = os.Stdout
		c = make(chan error, 1)
		b strings.Builder
	)

	r, w, err := os.Pipe()
	if err != nil {
		t.Fatal(err)
	}

	os.Stdout = w

	go func() {
		_, err := io.Copy(&b, r)
		r.Close()
		c <- err
	}()

	return func() string {
		os.Stdout = o
		w.Close()
		err := <-c
		if err != nil {
			t.Fatal(err)
		}
		return strings.TrimSpace(b.String())
	}
}

// fromTempDirectory is a cli-specific test helper which
// - creates a temp directory and changes to it as the new working directory
// - creates a temp directory and uses it for XDG_CONFIG_HOME
// - removes temp directories on cleanup
// - resets viper on cleanup (the reason this is "cli-specific")
func fromTempDirectory(t *testing.T) string {
	t.Helper()
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	d, done := Mktemp(t) // creates and CDs to 'tmp'
	t.Cleanup(func() { done(); viper.Reset() })
	return d
}
```

## `k8s/dialer.go` (changed lines (10, 25, 131, 141))

```go
package k8s

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"time"

	coreV1 "k8s.io/api/core/v1"
	metaV1 "k8s.io/apimachinery/pkg/apis/meta/v1"
	"k8s.io/apimachinery/pkg/fields"
	"k8s.io/apimachinery/pkg/util/rand"
	"k8s.io/apimachinery/pkg/watch"
	"k8s.io/client-go/kubernetes"
	"k8s.io/client-go/kubernetes/scheme"
	v1 "k8s.io/client-go/kubernetes/typed/core/v1"
	restclient "k8s.io/client-go/rest"
	"k8s.io/client-go/tools/remotecommand"
)

var SocatImage = "quay.io/boson/alpine-socat:1.7.4.3-r1-non-root"

// NewInClusterDialer creates context dialer that will dial TCP connections via POD running in k8s cluster.
// This is useful when accessing k8s services that are not exposed outside cluster (e.g. openshift image registry).
//
// Usage:
//
//     dialer, err := k8s.NewInClusterDialer(ctx)
//     if err != nil {
//         return err
//     }
//     defer dialer.Close()
//
//     transport := &http.Transport{
//         DialContext: dialer.DialContext,
//     }
//
//     var client = http.Client{
//         Transport: transport,
//     }
func NewInClusterDialer(ctx context.Context) (*contextDialer, error) {
	c := &contextDialer{
		detachChan: make(chan struct{}),
	}
	err := c.startDialerPod(ctx)
	if err != nil {
		return nil, err
	}
	return c, nil
}

type contextDialer struct {
	coreV1     v1.CoreV1Interface
	restConf   *restclient.Config
	podName    string
	namespace  string
	detachChan chan struct{}
}

func (c *contextDialer) DialContext(ctx context.Context, network string, addr string) (net.Conn, error) {
	if !(network == "tcp" || network == "tcp4" || network == "tcp6") {
		return nil, fmt.Errorf("unsupported network: %q", network)
	}

	execDone := make(chan struct{})
	pr, pw, conn := newConn(execDone)

	go func() {
		defer close(execDone)
		errOut := bytes.NewBuffer(nil)
		err := c.exec(addr, pr, pw, errOut)
		if err != nil {
			err = fmt.Errorf("failed to exec in pod: %w (stderr: %q)", err, errOut.String())
			_ = pr.CloseWithError(err)
			_ = pw.CloseWithError(err)
		}
	}()

	return conn, nil
}

func (c *contextDialer) Close() error {
	// closing the channel will cause stdin of the attached container to return EOF
	// as a result the pod exits -- it transits to Completed state
	close(c.detachChan)
	ctx, cancel := context.WithTimeout(context.Background(), time.Minute*1)
	defer cancel()
	delOpts := metaV1.DeleteOptions{}

	return c.coreV1.Pods(c.namespace).Delete(ctx, c.podName, delOpts)
}

func (c *contextDialer) startDialerPod(ctx context.Context) (err error) {
	cliConf := GetClientConfig()
	c.restConf, err = cliConf.ClientConfig()
	if err != nil {
		return
	}
	c.restConf.WarningHandler = restclient.NoWarnings{}

	err = setConfigDefaults(c.restConf)
	if err != nil {
		return
	}

	client, err := kubernetes.NewForConfig(c.restConf)
	if err != nil {
		return
	}
	c.coreV1 = client.CoreV1()

	c.namespace, err = GetNamespace("")
	if err != nil {
		return
	}

	pods := client.CoreV1().Pods(c.namespace)

	c.podName = "in-cluster-dialer-" + rand.String(5)

	defer func() {
		if err != nil {
			c.Close()
		}
	}()

	pod := &coreV1.Pod{
		ObjectMeta: metaV1.ObjectMeta{
			Name:        c.podName,
			Labels:      nil,
			Annotations: nil,
		},
		Spec: coreV1.PodSpec{
			Containers: []coreV1.Container{
				{
					Name:            c.podName,
					Image:           SocatImage,
					Stdin:           true,
					StdinOnce:       true,
					Command:         []string{"socat", "-u", "-", "OPEN:/dev/null"},
					SecurityContext: defaultSecurityContext(),
				},
			},
			DNSPolicy:     coreV1.DNSClusterFirst,
			RestartPolicy: coreV1.RestartPolicyNever,
		},
	}
	creatOpts := metaV1.CreateOptions{}

	ready := podReady(ctx, c.coreV1, c.podName, c.namespace)

	_, err = pods.Create(ctx, pod, creatOpts)
	if err != nil {
		return
	}

	select {
	case err = <-ready:
	case <-ctx.Done():
		err = ctx.Err()
	case <-time.After(time.Minute * 1):
		err = errors.New("timeout")
	}

	if err != nil {
		return fmt.Errorf("failed to start dialer container: %w", err)
	}

	// attaching to the stdin to automatically Complete the pod on exit
	go func() {
		_ = attach(c.coreV1.RESTClient(), c.restConf, c.podName, c.namespace, emptyBlockingReader(c.detachChan), io.Discard, io.Discard)
	}()

	return nil
}

// reader that returns no data and blocks until
// the channel is closed or data are sent to the channel
type emptyBlockingReader chan struct{}

func (e emptyBlockingReader) Read(p []byte) (n int, err error) {
	<-e
	return 0, io.EOF
}

func (c *contextDialer) exec(hostPort string, in io.Reader, out, errOut io.Writer) error {

	restClient := c.coreV1.RESTClient()
	req := restClient.Post().
		Resource("pods").
		Name(c.podName).
		Namespace(c.namespace).
		SubResource("exec")
	req.VersionedParams(&coreV1.PodExecOptions{
		Command:   []string{"socat", "-", fmt.Sprintf("TCP:%s", hostPort)},
		Container: c.podName,
		Stdin:     true,
		Stdout:    true,
		Stderr:    true,
		TTY:       false,
	}, scheme.ParameterCodec)

	executor, err := remotecommand.NewSPDYExecutor(c.restConf, "POST", req.URL())
	if err != nil {
		return err
	}

	return executor.Stream(remotecommand.StreamOptions{
		Stdin:  in,
		Stdout: out,
		Stderr: errOut,
		Tty:    false,
	})
}

func attach(restClient restclient.Interface, restConf *restclient.Config, podName, namespace string, in io.Reader, out, errOut io.Writer) error {
	req := restClient.Post().
		Resource("pods").
		Name(podName).
		Namespace(namespace).
		SubResource("attach")
	req.VersionedParams(&coreV1.PodAttachOptions{
		Container: podName,
		Stdin:     true,
		Stdout:    true,
		Stderr:    true,
		TTY:       false,
	}, scheme.ParameterCodec)

	executor, err := remotecommand.NewSPDYExecutor(restConf, "POST", req.URL())
	if err != nil {
		return err
	}

	return executor.Stream(remotecommand.StreamOptions{
		Stdin:  in,
		Stdout: out,
		Stderr: errOut,
		Tty:    false,
	})
}

func podReady(ctx context.Context, core v1.CoreV1Interface, podName, namespace string) (errChan <-chan error) {
	d := make(chan error)
	errChan = d

	pods := core.Pods(namespace)

	nameSelector := fields.OneTermEqualSelector("metadata.name", podName).String()
	listOpts := metaV1.ListOptions{
		Watch:         true,
		FieldSelector: nameSelector,
	}
	watcher, err := pods.Watch(ctx, listOpts)
	if err != nil {
		return
	}

	go func() {
		defer watcher.Stop()
		ch := watcher.ResultChan()
		for event := range ch {
			pod, ok := event.Object.(*coreV1.Pod)
			if !ok {
				continue
			}

			if event.Type == watch.Modified {
				for _, status := range pod.Status.ContainerStatuses {
					if status.Ready {
						d <- nil
						return
					}
					if status.State.Waiting != nil {
						switch status.State.Waiting.Reason {
						case "ErrImagePull",
							"CreateContainerError",
							"CreateContainerConfigError",
							"InvalidImageName",
							"CrashLoopBackOff",
							"ImagePullBackOff":
							d <- fmt.Errorf("reason: %v, message: %v",
								status.State.Waiting.Reason,
								status.State.Waiting.Message)
							return
						default:
							continue
						}
					}
				}
			}
		}
	}()

	return
}

func setConfigDefaults(config *restclient.Config) error {
	gv := coreV1.SchemeGroupVersion
	config.GroupVersion = &gv
	config.APIPath = "/api"
	config.NegotiatedSerializer = scheme.Codecs.WithoutConversion()

	if config.UserAgent == "" {
		config.UserAgent = restclient.DefaultKubernetesUserAgent()
	}

	return nil
}

type addr struct{}

func (a addr) Network() string {
	return "pod-stdio"
}

func (a addr) String() string {
	return "pod-stdio"
}

type conn struct {
	pr       *io.PipeReader
	pw       *io.PipeWriter
	execDone <-chan struct{}
}

func (c conn) Read(b []byte) (n int, err error) {
	return c.pr.Read(b)
}

func (c conn) Write(b []byte) (n int, err error) {
	return c.pw.Write(b)
}

func (c conn) Close() error {
	err := c.pw.Close()
	if err != nil {
		return fmt.Errorf("failed to close writer: %w", err)
	}
	<-c.execDone
	err = c.pr.Close()
	if err != nil {
		return fmt.Errorf("failed to close reader: %w", err)
	}
	return nil
}

func (c conn) LocalAddr() net.Addr {
	return addr{}
}

func (c conn) RemoteAddr() net.Addr {
	return addr{}
}

func (c conn) SetDeadline(t time.Time) error { return nil }

func (c conn) SetReadDeadline(t time.Time) error { return nil }

func (c conn) SetWriteDeadline(t time.Time) error { return nil }

func newConn(execDone <-chan struct{}) (*io.PipeReader, *io.PipeWriter, conn) {
	pr0, pw0 := io.Pipe()
	pr1, pw1 := io.Pipe()
	rwc := conn{pr: pr0, pw: pw1, execDone: execDone}
	return pr1, pw0, rwc
}

func NewLazyInitInClusterDialer() *lazyInitInClusterDialer {
	return &lazyInitInClusterDialer{}
}

type lazyInitInClusterDialer struct {
	contextDialer *contextDialer
	initErr       error
	o             sync.Once
}

func (l *lazyInitInClusterDialer) DialContext(ctx context.Context, network string, addr string) (net.Conn, error) {
	l.o.Do(func() {
		l.contextDialer, l.initErr = NewInClusterDialer(ctx)
	})
	if l.initErr != nil {
		return nil, l.initErr
	}
	return l.contextDialer.DialContext(ctx, network, addr)
}

func (l *lazyInitInClusterDialer) Close() error {
	if l.contextDialer != nil {
		return l.contextDialer.Close()
	}
	return nil
}
```
