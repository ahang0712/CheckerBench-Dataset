# Patch-overlapping Go context after the fix

## `pkg/engine/wildcards/wildcards.go` (changed lines (138, 139, 140, 141, 142, 143, 144, 145, 146, 147))

```go
package wildcards

import (
	"strings"

	"github.com/kyverno/kyverno/ext/wildcard"
	"github.com/kyverno/kyverno/pkg/engine/anchor"
	metav1 "k8s.io/apimachinery/pkg/apis/meta/v1"
)

// ReplaceInSelector replaces label selector keys and values containing
// wildcard characters with matching keys and values from the resource labels.
func ReplaceInSelector(labelSelector *metav1.LabelSelector, resourceLabels map[string]string) *metav1.LabelSelector {
	labelSelector = labelSelector.DeepCopy()
	result := replaceWildcardsInMapKeyValues(labelSelector.MatchLabels, resourceLabels)
	labelSelector.MatchLabels = result
	return labelSelector
}

// replaceWildcardsInMapKeyValues will expand  the "key" and "value" and will replace wildcard characters
// It also does not handle anchors as these are not expected in selectors
func replaceWildcardsInMapKeyValues(patternMap map[string]string, resourceMap map[string]string) map[string]string {
	result := map[string]string{}
	for k, v := range patternMap {
		if wildcard.ContainsWildcard(k) || wildcard.ContainsWildcard(v) {
			matchK, matchV := expandWildcards(k, v, resourceMap, true, true)
			result[matchK] = matchV
		} else {
			result[k] = v
		}
	}
	return result
}

func expandWildcards(k, v string, resourceMap map[string]string, matchValue, replace bool) (key string, val string) {
	for k1, v1 := range resourceMap {
		if wildcard.Match(k, k1) {
			if !matchValue {
				return k1, v1
			} else if wildcard.Match(v, v1) {
				return k1, v1
			}
		}
	}
	if replace {
		k = replaceWildCardChars(k)
		v = replaceWildCardChars(v)
	}
	return k, v
}

// replaceWildCardChars will replace '*' and '?' characters which are not
// supported by Kubernetes with a '0'.
func replaceWildCardChars(s string) string {
	s = strings.ReplaceAll(s, "*", "0")
	s = strings.ReplaceAll(s, "?", "0")
	return s
}

// ExpandInMetadata substitutes wildcard characters in map keys for metadata.labels and
// metadata.annotations that are present in a validation pattern. Values are not substituted
// here, as they are evaluated separately while processing the validation pattern. Anchors
// on the tags (e.g. "=(kubernetes.io/*)" will be preserved when the values are expanded.
func ExpandInMetadata(patternMap, resourceMap map[string]interface{}) map[string]interface{} {
	_, patternMetadata := getPatternValue("metadata", patternMap)
	if patternMetadata == nil {
		return patternMap
	}

	resourceMetadata := resourceMap["metadata"]
	if resourceMetadata == nil {
		return patternMap
	}

	metadata := patternMetadata.(map[string]interface{})
	labelsKey, labels := expandWildcardsInTag("labels", patternMetadata, resourceMetadata)
	if labels != nil {
		metadata[labelsKey] = labels
	}
	annotationsKey, annotations := expandWildcardsInTag("annotations", patternMetadata, resourceMetadata)
	if annotations != nil {
		metadata[annotationsKey] = annotations
	}
	return patternMap
}

func getPatternValue(tag string, pattern map[string]interface{}) (string, interface{}) {
	for k, v := range pattern {
		if k == tag {
			return k, v
		}
		if a := anchor.Parse(k); a != nil && a.Key() == tag {
			return k, v
		}
	}
	return "", nil
}

// expandWildcardsInTag
func expandWildcardsInTag(tag string, patternMetadata, resourceMetadata interface{}) (string, map[string]interface{}) {
	patternKey, patternData := getValueAsStringMap(tag, patternMetadata)
	if patternData == nil {
		return "", nil
	}

	_, resourceData := getValueAsStringMap(tag, resourceMetadata)
	if resourceData == nil {
		return "", nil
	}

	results := replaceWildcardsInMapKeys(patternData, resourceData)
	return patternKey, results
}

func getValueAsStringMap(key string, data interface{}) (string, map[string]string) {
	if data == nil {
		return "", nil
	}

	dataMap, ok := data.(map[string]interface{})
	if !ok {
		return "", nil
	}
	patternKey, val := getPatternValue(key, dataMap)

	if val == nil {
		return "", nil
	}

	result := map[string]string{}

	valMap, ok := val.(map[string]interface{})
	if !ok {
		return "", nil
	}

	for k, v := range valMap {
		if v == nil {
			continue
		}

		switch typedVal := v.(type) {
		case string:
			result[k] = typedVal
		default:
			continue
		}
	}

	return patternKey, result
}

// replaceWildcardsInMapKeys will expand only the "key" and not replace wildcard characters in the key or values
// It also preserves anchors in keys
func replaceWildcardsInMapKeys(patternData, resourceData map[string]string) map[string]interface{} {
	results := map[string]interface{}{}
	for k, v := range patternData {
		if wildcard.ContainsWildcard(k) {
			if a := anchor.Parse(k); a != nil {
				matchK, _ := expandWildcards(a.Key(), v, resourceData, false, false)
				results[anchor.String(a.Type(), matchK)] = v
			} else {
				matchK, _ := expandWildcards(k, v, resourceData, false, false)
				results[matchK] = v
			}
		} else {
			results[k] = v
		}
	}
	return results
}
```

## `pkg/engine/wildcards/wildcards_test.go` (changed lines (6, 7, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 267))

```go
package wildcards

import (
	"reflect"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestExpandInMetadata(t *testing.T) {
	// testExpand(t, map[string]string{"test/*": "*"}, map[string]string{},
	//	map[string]string{"test/0": "0"})

	testExpand(t, map[string]string{"test/*": "*"}, map[string]string{"test/test": "test"},
		map[string]interface{}{"test/test": "*"})

	testExpand(t, map[string]string{"=(test/*)": "test"}, map[string]string{"test/test": "test"},
		map[string]interface{}{"=(test/test)": "test"})

	testExpand(t, map[string]string{"test/*": "*"}, map[string]string{"test/test1": "test1"},
		map[string]interface{}{"test/test1": "*"})
}

func testExpand(t *testing.T, patternMap, resourceMap map[string]string, expectedMap map[string]interface{}) {
	result := replaceWildcardsInMapKeys(patternMap, resourceMap)
	if !reflect.DeepEqual(expectedMap, result) {
		t.Errorf("expected %v but received %v", expectedMap, result)
	}
}

func TestGetValueAsStringMap_NilHandling(t *testing.T) {
	tests := []struct {
		name           string
		key            string
		data           interface{}
		expectedKey    string
		expectedResult map[string]string
	}{
		{
			name:           "nil data",
			key:            "test",
			data:           nil,
			expectedKey:    "",
			expectedResult: nil,
		},
		{
			name:           "data is not a map",
			key:            "test",
			data:           "not a map",
			expectedKey:    "",
			expectedResult: nil,
		},
		{
			name:           "key not found",
			key:            "nonexistent",
			data:           map[string]interface{}{"otherKey": "value"},
			expectedKey:    "",
			expectedResult: nil,
		},
		{
			name:           "value is nil",
			key:            "test",
			data:           map[string]interface{}{"test": nil},
			expectedKey:    "",
			expectedResult: nil,
		},
		{
			name:           "value is not a map",
			key:            "test",
			data:           map[string]interface{}{"test": "not a map"},
			expectedKey:    "",
			expectedResult: nil,
		},
		{
			name: "handles nil value in map",
			key:  "test",
			data: map[string]interface{}{
				"test": map[string]interface{}{
					"key1": "value1",
					"key2": nil,
					"key3": "value3",
				},
			},
			expectedKey: "test",
			expectedResult: map[string]string{
				"key1": "value1",
				// key2 should be skipped
				"key3": "value3",
			},
		},
		{
			name: "handles non-string value in map",
			key:  "test",
			data: map[string]interface{}{
				"test": map[string]interface{}{
					"key1": "value1",
					"key2": 123,
					"key3": map[string]string{
						"nested": "value",
					},
					"key4": "value4",
				},
			},
			expectedKey: "test",
			expectedResult: map[string]string{
				"key1": "value1",
				// key2 should be skipped (non-string)
				// key3 should be skipped (complex)
				"key4": "value4",
			},
		},
		{
			name: "normal case - all strings",
			key:  "test",
			data: map[string]interface{}{
				"test": map[string]interface{}{
					"key1": "value1",
					"key2": "value2",
				},
			},
			expectedKey: "test",
			expectedResult: map[string]string{
				"key1": "value1",
				"key2": "value2",
			},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			key, result := getValueAsStringMap(test.key, test.data)
			assert.Equal(t, test.expectedKey, key)
			assert.Equal(t, test.expectedResult, result)
		})
	}
}

func TestExpandInMetadata_NilSafety(t *testing.T) {
	testCases := []struct {
		name        string
		patternMap  map[string]interface{}
		resourceMap map[string]interface{}
		shouldPanic bool
	}{
		{
			name: "nil value in annotation should not panic",
			patternMap: map[string]interface{}{
				"metadata": map[string]interface{}{
					"annotations": map[string]interface{}{
						"some-key": nil,
					},
				},
			},
			resourceMap: map[string]interface{}{
				"metadata": map[string]interface{}{
					"annotations": map[string]interface{}{
						"real-key": "real-value",
					},
				},
			},
			shouldPanic: false,
		},
		{
			name: "complex value in annotation should not panic",
			patternMap: map[string]interface{}{
				"metadata": map[string]interface{}{
					"annotations": map[string]interface{}{
						"some-key": map[string]interface{}{
							"nested": "value",
						},
					},
				},
			},
			resourceMap: map[string]interface{}{
				"metadata": map[string]interface{}{
					"annotations": map[string]interface{}{
						"real-key": "real-value",
					},
				},
			},
			shouldPanic: false,
		},
		{
			name: "simulated jmespath nil result",
			patternMap: map[string]interface{}{
				"metadata": map[string]interface{}{
					"annotations": map[string]interface{}{
						"test": nil, // Simulating what happens when {{@ | foo}} evaluates with undefined 'foo'
					},
				},
			},
			resourceMap: map[string]interface{}{
				"metadata": map[string]interface{}{
					"annotations": map[string]interface{}{
						"real-key": "real-value",
					},
				},
			},
			shouldPanic: false,
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			defer func() {
				r := recover()
				if tc.shouldPanic {
					assert.NotNil(t, r, "Expected function to panic, but it didn't")
				} else {
					assert.Nil(t, r, "Function panicked unexpectedly")
				}
			}()

			ExpandInMetadata(tc.patternMap, tc.resourceMap)
		})
	}
}

func TestJMESPathNil(t *testing.T) {
	// Create a pattern with a nil value in labels or annotations
	// to simulate what happens after a JMESPath expression like {{@ | foo}}
	// (where 'foo' is not a defined function) is evaluated and results in nil.
	patternMap := map[string]interface{}{
		"metadata": map[string]interface{}{
			"labels": map[string]interface{}{
				"normal":    "value",
				"nil-value": nil, // This represents the result of {{@ | foo}} substitution
			},
			"annotations": map[string]interface{}{
				"another-normal": "value",
				"complex-value": map[string]interface{}{ // And this represents a complex structure
					"nested": "value",
				},
			},
		},
	}

	resourceMap := map[string]interface{}{
		"metadata": map[string]interface{}{
			"labels": map[string]interface{}{
				"app": "test",
			},
			"annotations": map[string]interface{}{
				"test": "value",
			},
		},
	}

	defer func() {
		r := recover()
		assert.Nil(t, r, "ExpandInMetadata should not panic with nil values, but it did: %v", r)
	}()

	result := ExpandInMetadata(patternMap, resourceMap)

	// Additional verification that the function works correctly
	metadataResult := result["metadata"].(map[string]interface{})
	labelsResult, ok := metadataResult["labels"].(map[string]interface{})

	assert.True(t, ok, "Expected labels to be a map[string]interface{}")
	assert.Contains(t, labelsResult, "normal")

	// Annotations with complex values should also be handled properly
	annotationsResult, ok := metadataResult["annotations"].(map[string]interface{})
	assert.True(t, ok, "Expected annotations to be a map[string]interface{}")
	assert.Contains(t, annotationsResult, "another-normal")
}
```
