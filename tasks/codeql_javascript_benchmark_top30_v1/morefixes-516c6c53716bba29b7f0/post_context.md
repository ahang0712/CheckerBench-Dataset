# Patch-overlapping JavaScript context after the fix

## `decidim-templates/app/packs/src/decidim/templates/admin/choose_template.js` (changed lines (16, 17, 18, 19))

```javascript
import { datalistSelect } from "src/decidim/datalist_select";

$(() => {
  const wrapper = document.querySelector("#choose-template");
  if (!wrapper) {
    return;
  }

  const preview = (id) => {
    const options = wrapper.dataset;
    const previewURL = options.previewurl;
    if (!previewURL) {
      return;
    }
    const params = new URLSearchParams({ id: id });
    Rails.ajax({
      url: `${previewURL}?${params.toString()}`,
      type: "GET",
      error: (data) => (console.error(data))
    });
  }

  datalistSelect(wrapper, preview)
})
```
