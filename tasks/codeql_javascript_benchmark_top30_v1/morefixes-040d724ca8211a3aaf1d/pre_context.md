# Patch-overlapping JavaScript context before the fix

## `src/web/assets/quickpost/dist/QuickPostWidget.js` (changed lines (1, 2))

```javascript
!function(){var t;t=jQuery,Craft.QuickPostWidget=Garnish.Base.extend({params:null,initFields:null,formHtml:null,$widget:null,$form:null,$spinner:null,$errorList:null,loading:!1,init:function(i,r,e,n){this.params=r,this.initFields=e,this.formHtml=n,this.$widget=t("#widget"+i),this.initForm(this.$widget.find("form:first"))},initForm:function(t){this.$form=t,this.$spinner=this.$form.find(".spinner"),this.initFields();var i=this.$form.find("> .buttons > .btngroup > .menubtn"),r=i.data("menubtn").menu.$container.find("> ul > li > a");i.menubtn(),this.addListener(this.$form,"submit","handleFormSubmit"),this.addListener(r,"click","saveAndContinueEditing")},handleFormSubmit:function(t){t.preventDefault(),this.save(this.onSave.bind(this))},saveAndContinueEditing:function(){this.save(this.gotoEntry.bind(this))},save:function(i){var r=this;if(!this.loading){this.loading=!0,this.$spinner.removeClass("hidden");var e=Garnish.getPostData(this.$form),n=t.extend({enabled:1},e,this.params);Craft.postActionRequest("entries/save-entry",n,(function(e,n){if(r.loading=!1,r.$spinner.addClass("hidden"),r.$errorList&&r.$errorList.children().remove(),"success"===n)if(e.success)Craft.cp.displayNotice(Craft.t("app","Entry saved.")),i(e);else if(Craft.cp.displayError(Craft.t("app","Couldn’t save entry.")),e.errors)for(var s in r.$errorList||(r.$errorList=t('<ul class="errors"/>').insertAfter(r.$form)),e.errors)if(e.errors.hasOwnProperty(s))for(var a=0;a<e.errors[s].length;a++){var o=e.errors[s][a];t("<li>"+o+"</li>").appendTo(r.$errorList)}}))}},onSave:function(i){var r=t(this.formHtml);if(this.$form.replaceWith(r),Craft.initUiElements(r),this.initForm(r),void 0!==Craft.RecentEntriesWidget)for(var e=0;e<Craft.RecentEntriesWidget.instances.length;e++){var n=Craft.RecentEntriesWidget.instances[e];n.params.sectionId&&n.params.sectionId!=this.params.sectionId||n.addEntry({url:i.cpEditUrl,title:i.title,dateCreated:i.dateCreated,username:i.authorUsername})}},gotoEntry:function(t){Craft.redirectTo(t.cpEditUrl)}})}();
//# sourceMappingURL=QuickPostWidget.js.map
```

## `src/web/assets/quickpost/src/QuickPostWidget.js` (changed lines (97, 98))

```javascript
(function ($) {
  /** global: Craft */
  /** global: Garnish */
  Craft.QuickPostWidget = Garnish.Base.extend({
    params: null,
    initFields: null,
    formHtml: null,
    $widget: null,
    $form: null,
    $spinner: null,
    $errorList: null,
    loading: false,

    init: function (widgetId, params, initFields, formHtml) {
      this.params = params;
      this.initFields = initFields;
      this.formHtml = formHtml;
      this.$widget = $('#widget' + widgetId);

      this.initForm(this.$widget.find('form:first'));
    },

    initForm: function ($form) {
      this.$form = $form;
      this.$spinner = this.$form.find('.spinner');

      this.initFields();

      var $menuBtn = this.$form.find('> .buttons > .btngroup > .menubtn'),
        $saveAndContinueEditingBtn = $menuBtn
          .data('menubtn')
          .menu.$container.find('> ul > li > a');

      $menuBtn.menubtn();

      this.addListener(this.$form, 'submit', 'handleFormSubmit');
      this.addListener(
        $saveAndContinueEditingBtn,
        'click',
        'saveAndContinueEditing'
      );
    },

    handleFormSubmit: function (event) {
      event.preventDefault();

      this.save(this.onSave.bind(this));
    },

    saveAndContinueEditing: function () {
      this.save(this.gotoEntry.bind(this));
    },

    save: function (callback) {
      if (this.loading) {
        return;
      }

      this.loading = true;
      this.$spinner.removeClass('hidden');

      var formData = Garnish.getPostData(this.$form),
        data = $.extend({enabled: 1}, formData, this.params);

      Craft.postActionRequest(
        'entries/save-entry',
        data,
        (response, textStatus) => {
          this.loading = false;
          this.$spinner.addClass('hidden');

          if (this.$errorList) {
            this.$errorList.children().remove();
          }

          if (textStatus === 'success') {
            if (response.success) {
              Craft.cp.displayNotice(Craft.t('app', 'Entry saved.'));
              callback(response);
            } else {
              Craft.cp.displayError(Craft.t('app', 'Couldn’t save entry.'));

              if (response.errors) {
                if (!this.$errorList) {
                  this.$errorList = $('<ul class="errors"/>').insertAfter(
                    this.$form
                  );
                }

                for (var attribute in response.errors) {
                  if (!response.errors.hasOwnProperty(attribute)) {
                    continue;
                  }

                  for (var i = 0; i < response.errors[attribute].length; i++) {
                    var error = response.errors[attribute][i];
                    $('<li>' + error + '</li>').appendTo(this.$errorList);
                  }
                }
              }
            }
          }
        }
      );
    },

    onSave: function (response) {
      // Reset the widget
      var $newForm = $(this.formHtml);
      this.$form.replaceWith($newForm);
      Craft.initUiElements($newForm);
      this.initForm($newForm);

      // Are there any Recent Entries widgets to notify?
      if (typeof Craft.RecentEntriesWidget !== 'undefined') {
        for (var i = 0; i < Craft.RecentEntriesWidget.instances.length; i++) {
          var widget = Craft.RecentEntriesWidget.instances[i];
          if (
            !widget.params.sectionId ||
            widget.params.sectionId == this.params.sectionId
          ) {
            widget.addEntry({
              url: response.cpEditUrl,
              title: response.title,
              dateCreated: response.dateCreated,
              username: response.authorUsername,
            });
          }
        }
      }
    },

    gotoEntry: function (response) {
      // Redirect to the entry's edit URL
      Craft.redirectTo(response.cpEditUrl);
    },
  });
})(jQuery);
```
