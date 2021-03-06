from PIL import  Image
from torchvision import transforms
from torch import topk, load

if __name__ == "__main__":
    img = Image.open("download.jpg").convert('RGB')
    #trans2 = transforms.Resize((299,299))
    #trans3 = transforms.ToTensor()
    #normalize = transforms.Normalize(mean=[0.5, 0.5, 0.5],
                                     #std=[0.5, 0.5, 0.5]) # Found in debugger
    #im_tense = trans2(normalize(trans3(img)))
    preprocessing = transforms.Compose([
        transforms.Resize(299),
        transforms.ToTensor(),
        transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])])
    im_tense = preprocessing(img)

    """model = resnet34(pretrained=False)
    model.fc = Linear(512,196)
    not_state = torch.load("saved/training/ResNet34-AutoAugment/1028_205522/checkpoint-epoch10.pth",map_location=lambda storage, loc: storage)
    state_dict = not_state['state_dict']
    model.load_state_dict(state_dict,strict=True)"""
    model = load("saved/cpufinal.pth")
    model.eval()
    """gpumodel = torch.load("saved/final.pth")
    gpumodel = gpumodel.to('cuda')
    gpumodel.eval()
    gpu_tense = im_tense.to('cuda')"""
    output = model(im_tense.expand(1,im_tense.shape[0],im_tense.shape[1],im_tense.shape[2]))
    #print(im_tense.shape)
    #output = functional.softmax(output)
    #gpu_output = gpumodel(gpu_tense.expand(1,gpu_tense.shape[0],gpu_tense.shape[1],gpu_tense.shape[2]))
    results = topk(output,30)[1][0].tolist()
    predictions = []
    for i in range(30):
        if(results[i] > 195):
            predictions.append(results[i])
            break
    if len(predictions) == 1:
        predictions.append(results[0])
        predictions.append(results[1])
    if len(predictions) == 0:
        predictions.append(results[0])
        predictions.append(results[1])
        predictions.append(results[2])
    print(predictions)
    #print(topk(gpu_output, 10))